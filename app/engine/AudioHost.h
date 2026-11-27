#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <dsp/RenderEngine.h>
#include <ipc/AppEndpoint.h>

struct AudioHostOptions
{
    bool openDevice = true;                                         // false for UI tests
    juce::String sharedMemoryName = locus::ipc::defaultSharedMemoryName;
    const juce::XmlElement* savedDeviceState = nullptr;             // from the settings file
};

/** Owns the audio device, the render engine and the connection to the Send plugins, and runs
    them in the device callback.

    In debug builds only, source 1 plays a test signal (noise bursts circling the listener)
    while no Send plugin holds slot 1.
*/
class AudioHost final : public juce::AudioIODeviceCallback,
                        private juce::Timer
{
public:
    explicit AudioHost (const AudioHostOptions& options = {});
    ~AudioHost() override;

    locus::RenderEngine& getEngine() noexcept               { return engine; }
    locus::ipc::AppEndpoint& getEndpoint() noexcept         { return endpoint; }
    juce::AudioDeviceManager& getDeviceManager() noexcept   { return deviceManager; }

    /** Error from opening the audio device, if any. */
    juce::String getDeviceError() const { return deviceError; }

    /** True while the debug-only test signal is playing on source 1. */
    bool isTestSignalActive() const noexcept { return testSignalActive.load (std::memory_order_relaxed); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}

    void timerCallback() override;

    juce::AudioDeviceManager deviceManager;
    locus::RenderEngine engine;
    locus::ipc::AppEndpoint endpoint;
    std::array<locus::SourceParamStore*, locus::maxSources> stores {};
    juce::String deviceError;
    std::atomic<bool> testSignalActive { false };

   #if JUCE_DEBUG
    std::vector<float> testSignal;
    juce::Random testNoise;
    double testSampleRate = 48000.0;
    juce::int64 testPosition = 0;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioHost)
};
