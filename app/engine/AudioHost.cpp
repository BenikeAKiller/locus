#include "AudioHost.h"

namespace
{
    constexpr int maxOutputChannels = locus::maxSpeakers;
    constexpr int syncTimerHz = 30;
}

AudioHost::AudioHost (const AudioHostOptions& options)
    : endpoint (options.sharedMemoryName)
{
    for (int s = 0; s < locus::maxSources; ++s)
        stores[(size_t) s] = &engine.getSourceParams (s);

    if (options.openDevice)
    {
        deviceError = deviceManager.initialise (0, maxOutputChannels, options.savedDeviceState, true);
        deviceManager.addAudioCallback (this);
        startTimerHz (syncTimerHz);
    }
}

AudioHost::~AudioHost()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioHost::timerCallback()
{
    endpoint.tick (juce::Time::getMillisecondCounterHiRes() * 0.001, stores);
    engine.releaseRetiredLayouts();
}

void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    const auto bufferSize = device->getCurrentBufferSizeSamples();

    engine.prepare (sampleRate, bufferSize);
    endpoint.prepare (sampleRate, bufferSize);

   #if JUCE_DEBUG
    testSignal.assign ((size_t) std::max (bufferSize, 8192), 0.0f);
    testSampleRate = sampleRate;
    testPosition = 0;
   #endif
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                  float* const* outputs, int numOutputs, int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    locus::SourceInputs inputs {};
    endpoint.pullAudio (inputs, numSamples);

   #if JUCE_DEBUG
    // Test signal on source 1 while no Send plugin holds that slot: 250 ms noise bursts at
    // -18 dBFS, twice a second, circling the listener at 45 degrees per second.
    const bool playTest = ! endpoint.isSlotOwned (0) && (size_t) numSamples <= testSignal.size();
    testSignalActive.store (playTest, std::memory_order_relaxed);

    if (playTest)
    {
        const auto burstLength = (juce::int64) (0.25 * testSampleRate);
        const auto period = (juce::int64) (0.5 * testSampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inBurst = (testPosition + i) % period < burstLength;
            testSignal[(size_t) i] = inBurst ? 0.125f * (testNoise.nextFloat() * 2.0f - 1.0f) : 0.0f;
        }

        const auto seconds = (double) testPosition / testSampleRate;
        engine.getSourceParams (0).set (locus::SourceParam::azimuth, (float) locus::wrapAzimuth (seconds * 45.0));
        testPosition += numSamples;

        inputs[0].channels[0] = testSignal.data();
        inputs[0].numChannels = 1;
    }
   #endif

    engine.process (inputs, outputs, numOutputs, numSamples);
}
