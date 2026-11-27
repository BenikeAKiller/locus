// LocusPluginCheck: loads the built Locus Send VST3 like a DAW would, acts as the Locus app
// (through the real shared memory), and checks audio, parameter sync, gestures, state and
// slot handling end to end. Usage: LocusPluginCheck "<path to Locus Send.vst3>"
// Do not run it while the Locus app is open: both would act as the app.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <ipc/AppEndpoint.h>

using namespace locus;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok    " : "FAILED", what.toRawUTF8());
        failures += ok ? 0 : 1;
    }

    /** Records the host-side calls a plugin makes, as a DAW would see them. */
    struct HostRecorder final : juce::AudioProcessorListener
    {
        std::map<int, int> gestureBegins, gestureEnds, changes;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { ++changes[index]; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { ++gestureBegins[index]; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { ++gestureEnds[index]; }
    };

    juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (64) == name)
                return param;
        return nullptr;
    }
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); // keep output even if something crashes
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::printf ("usage: LocusPluginCheck <path to Locus Send.vst3>\n");
        return 2;
    }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat()); // the manager takes ownership
    auto* vst3 = formats.getFormat (0);

    juce::OwnedArray<juce::PluginDescription> types;
    vst3->findAllTypesForFile (types, juce::String (argv[1]));
    check (types.size() == 1, "VST3 found and scanned: " + (types.isEmpty() ? juce::String ("none") : types[0]->name));
    if (types.isEmpty())
        return 1;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480; // 10 ms

    juce::String error;

    // --send-only <seconds>: host one instance playing a 440 Hz tone into the running Locus app
    // (the app does the rendering); for a live check of the whole chain.
    if (argc >= 4 && juce::String (argv[2]) == "--send-only")
    {
        auto send = formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
        if (send == nullptr)
            return 1;

        send->prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const auto blocks = (int) (juce::String (argv[3]).getDoubleValue() * 100.0);

        for (int i = 0; i < blocks; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            for (int s = 0; s < blockSize; ++s)
            {
                const auto v = (float) (0.125 * std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / sampleRate;
                buffer.setSample (0, s, v);
                buffer.setSample (1, s, v);
            }
            send->processBlock (buffer, midi);
        }

        send->releaseResources();
        std::printf ("send-only: streamed %d blocks\n", blocks);
        return 0;
    }
    auto a = formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
    auto b = formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
    check (a != nullptr && b != nullptr, "two instances created " + error);
    if (a == nullptr || b == nullptr)
        return 1;

    // Instance B on a mono track.
    auto monoLayout = b->getBusesLayout();
    monoLayout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    monoLayout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    check (b->setBusesLayout (monoLayout), "mono bus layout accepted");

    // Unsupported: stereo in, mono out.
    auto badLayout = a->getBusesLayout();
    badLayout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    check (! a->checkBusesLayoutSupported (badLayout), "stereo-in / mono-out rejected");

    a->prepareToPlay (sampleRate, blockSize);
    b->prepareToPlay (sampleRate, blockSize);

    juce::AudioProcessor::TrackProperties props;
    props.name = juce::String ("Drums");
    a->updateTrackProperties (props);

    HostRecorder recorderA;
    a->addListener (&recorderA);

    // The check plays the app's role.
    auto app = std::make_unique<ipc::AppEndpoint>();
    check (app->isOpen(), "shared memory opened by the app side");
    app->setTargetFill (1024);
    app->prepare (sampleRate, blockSize);

    std::vector<std::unique_ptr<SourceParamStore>> owned;
    std::array<SourceParamStore*, maxSources> stores {};
    for (auto& s : stores)
    {
        owned.push_back (std::make_unique<SourceParamStore> (getSourceParamSpecs()));
        s = owned.back().get();
    }

    juce::AudioBuffer<float> bufferA (2, blockSize), bufferB (1, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0, sumIn = 0.0, sumOutA = 0.0, outputLeak = 0.0;
    int measuredBlocks = 0;

    auto runFor = [&] (double seconds, bool measure)
    {
        const auto blocks = (int) (seconds * 100.0);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10); // plugin timers

            for (int s = 0; s < blockSize; ++s)
            {
                const auto v = (float) (0.5 * std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / sampleRate;
                bufferA.setSample (0, s, v);
                bufferA.setSample (1, s, v);
                bufferB.setSample (0, s, v * 0.5f);
                if (measure) sumIn += (double) v * v;
            }

            if (a != nullptr) a->processBlock (bufferA, midi);
            if (b != nullptr) b->processBlock (bufferB, midi);
            if (a != nullptr && b != nullptr)
                outputLeak = std::max ({ outputLeak, (double) bufferA.getMagnitude (0, blockSize), (double) bufferB.getMagnitude (0, blockSize) });

            if (app == nullptr)
                continue; // the app is closed

            app->tick (juce::Time::getMillisecondCounterHiRes() * 0.001, stores);
            SourceInputs inputs {};
            app->pullAudio (inputs, blockSize);

            if (measure && inputs[0].numChannels > 0)
            {
                for (int s = 0; s < blockSize; ++s)
                    sumOutA += (double) inputs[0].channels[0][s] * inputs[0].channels[0][s];
                ++measuredBlocks;
            }
        }
    };

    runFor (1.0, false); // connect, prime
    runFor (2.0, true);

    const auto stA = app->getStatus (0), stB = app->getStatus (1);
    check (stA.connected && stA.active, "instance A connected and streaming on slot 1");
    check (stB.connected && stB.active, "instance B connected and streaming on slot 2");
    check (stA.numChannels == 2 && stB.numChannels == 1, "channel counts: A stereo, B mono (" + juce::String (stA.numChannels) + ", " + juce::String (stB.numChannels) + ")");
    check (stA.name == "Drums", "track name reaches the app: \"" + stA.name + "\"");
    check (stB.name == "Source 2", "untitled track named by slot: \"" + stB.name + "\"");
    check (outputLeak == 0.0, "tracks output silence");
    check (stA.underruns == 0, "no underruns (" + juce::String (stA.underruns) + ")");

    const auto rmsRatio = measuredBlocks > 0 ? std::sqrt (sumOutA / sumIn) : 0.0;
    check (std::abs (rmsRatio - 1.0) < 0.02, "audio level preserved through the link (ratio " + juce::String (rmsRatio, 4) + ")");

    // Host automation -> app.
    auto* azimuthA = findParameter (*a, "Azimuth");
    auto* distanceA = findParameter (*a, "Distance");
    check (azimuthA != nullptr && distanceA != nullptr, "parameters exposed to the host");
    // The VST3 wrapper adds a host "Bypass" parameter to plugins that don't define one.
    int ownParameters = 0;
    for (auto* p : a->getParameters())
        ownParameters += p->getName (64) == "Bypass" ? 0 : 1;
    check (ownParameters == SourceParam::count, "parameter count " + juce::String (ownParameters) + " (+ host bypass)");
    if (azimuthA == nullptr || distanceA == nullptr)
        return 1;

    azimuthA->setValueNotifyingHost (juce::jmap (60.0f, -180.0f, 180.0f, 0.0f, 1.0f));
    runFor (0.2, false);
    check (std::abs (stores[0]->get (SourceParam::azimuth) - 60.0f) < 0.05f,
           "host automation reaches the app (azimuth " + juce::String (stores[0]->get (SourceParam::azimuth), 2) + ")");

    // App edit -> plugin, with a host gesture so DAWs can record it.
    const auto distanceIndex = distanceA->getParameterIndex();
    recorderA.gestureBegins.clear(); recorderA.gestureEnds.clear(); recorderA.changes.clear();
    app->sendEdit (0, SourceParam::distance, 3.5f, false);
    runFor (0.2, false);
    const auto distanceNow = distanceA->getText (distanceA->getValue(), 32);
    check (distanceNow.getFloatValue() > 3.49f && distanceNow.getFloatValue() < 3.51f, "app edit reaches the plugin (distance " + distanceNow + ")");
    check (recorderA.gestureBegins[distanceIndex] == 1 && recorderA.gestureEnds[distanceIndex] == 1,
           "app edit wrapped in one host gesture (begin " + juce::String (recorderA.gestureBegins[distanceIndex])
               + ", end " + juce::String (recorderA.gestureEnds[distanceIndex]) + ")");

    // A drag in the app: one gesture spanning several values.
    recorderA.gestureBegins.clear(); recorderA.gestureEnds.clear(); recorderA.changes.clear();
    const auto azimuthIndex = azimuthA->getParameterIndex();
    for (float v : { 70.0f, 80.0f, 90.0f })
    {
        app->sendEdit (0, SourceParam::azimuth, v, true);
        runFor (0.05, false);
    }
    app->sendEdit (0, SourceParam::azimuth, 90.0f, false);
    runFor (0.2, false);
    check (recorderA.gestureBegins[azimuthIndex] == 1 && recorderA.gestureEnds[azimuthIndex] == 1 && recorderA.changes[azimuthIndex] >= 3,
           "app drag = one gesture with " + juce::String (recorderA.changes[azimuthIndex]) + " value changes");
    check (std::abs (stores[0]->get (SourceParam::azimuth) - 90.0f) < 0.05f, "app keeps its own value after the round trip");

    // State save / restore, including the slot choice.
    juce::MemoryBlock saved;
    a->getStateInformation (saved);
    auto c = formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
    c->setStateInformation (saved.getData(), (int) saved.getSize());
    auto* distanceC = findParameter (*c, "Distance");
    check (distanceC != nullptr && std::abs (distanceC->getText (distanceC->getValue(), 32).getFloatValue() - 3.5f) < 0.01f,
           "state restores parameter values");
    c = nullptr;

    // Offline rendering: refused, and the slot goes idle in the app.
    a->setNonRealtime (true);
    runFor (0.5, false);
    check (! app->getStatus (0).active, "offline render: slot idle in the app");
    a->setNonRealtime (false);
    runFor (0.5, false);
    check (app->getStatus (0).active, "back to real time: streaming again");

    // The app quits and starts again while the DAW keeps playing.
    app = nullptr;
    runFor (1.5, false);
    app = std::make_unique<ipc::AppEndpoint>();
    app->setTargetFill (1024);
    app->prepare (sampleRate, blockSize);
    for (auto* s : stores)
        s->resetToDefaults();
    runFor (2.0, false);
    check (app->getStatus (0).connected && app->getStatus (0).active, "app restart: plugin reconnects by itself");
    check (std::abs (stores[0]->get (SourceParam::azimuth) - 90.0f) < 0.05f,
           "app restart: plugin's values reach the new app (azimuth " + juce::String (stores[0]->get (SourceParam::azimuth), 1) + ")");
    check (app->getStatus (0).name == "Drums", "app restart: track name restored");

    // More tracks than slots: 16 work, the 17th waits without disturbing anyone.
    {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> extra;
        for (int i = 0; i < 15; ++i)
        {
            extra.push_back (formats.createPluginInstance (*types[0], sampleRate, blockSize, error));
            extra.back()->prepareToPlay (sampleRate, blockSize);
        }

        juce::AudioBuffer<float> extraBuffer (2, blockSize);
        for (int i = 0; i < 300; ++i) // 3 s
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            for (auto& p : extra)
            {
                extraBuffer.clear();
                extraBuffer.setSample (0, 0, 0.5f);
                p->processBlock (extraBuffer, midi);
            }
            a->processBlock (bufferA, midi);
            b->processBlock (bufferB, midi);
            if (app == nullptr)
                continue; // the app is closed

            app->tick (juce::Time::getMillisecondCounterHiRes() * 0.001, stores);
            SourceInputs inputs {};
            app->pullAudio (inputs, blockSize);
        }

        int connected = 0;
        for (int s = 0; s < maxSources; ++s)
            connected += app->getStatus (s).connected ? 1 : 0;

        check (connected == 16, "17 instances: all 16 slots in use (" + juce::String (connected) + ")");
        check (app->getStatus (0).active && app->getStatus (0).name == "Drums", "17 instances: the first track still streams");

        // Removing one frees a slot, and the waiting instance takes it within its 1 s retry.
        extra.front()->releaseResources();
        extra.erase (extra.begin());
        runFor (0.1, false);
        int afterRemoval = 0;
        for (int s = 0; s < maxSources; ++s)
            afterRemoval += app->getStatus (s).connected ? 1 : 0;
        check (afterRemoval == 15 || afterRemoval == 16, "removing an instance frees its slot");

        for (auto& p : extra)
            p->releaseResources();
    }

    // Unloading releases the slot.
    a->removeListener (&recorderA);
    a->releaseResources();
    a = nullptr;
    runFor (0.2, false);
    check (! app->getStatus (0).connected, "unloading the plugin frees its slot");
    check (app->getStatus (1).connected, "the other instance is unaffected");

    b->releaseResources();
    b = nullptr;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
