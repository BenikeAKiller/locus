#include <ipc/AppEndpoint.h>
#include <ipc/AudioRing.h>
#include <ipc/SendEndpoint.h>
#include <thread>

using namespace locus;
using namespace locus::ipc;

namespace
{
    /** A private shared-memory name per test, so a running Locus app is never touched. */
    juce::String uniqueName()
    {
        return "LocusTest_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    }

    std::array<SourceParamStore*, maxSources> storePointers (std::vector<std::unique_ptr<SourceParamStore>>& stores)
    {
        std::array<SourceParamStore*, maxSources> p {};
        for (int i = 0; i < maxSources; ++i)
        {
            stores.push_back (std::make_unique<SourceParamStore> (getSourceParamSpecs()));
            p[(size_t) i] = stores.back().get();
        }
        return p;
    }

    std::vector<float> defaultValues()
    {
        std::vector<float> v;
        for (const auto& s : getSourceParamSpecs())
            v.push_back (s.defaultValue);
        return v;
    }

    struct LinkResult
    {
        int underruns = 0;
        int gaps = 0;      // silent app blocks after audio started, in a continuous tone
        int target = 0;
    };

    /** Streams a continuous tone from a simulated DAW into a simulated app, each on its own
        clock, and counts dropouts. pluginBlock <= 0 means random block sizes 1..-pluginBlock
        (hosts like FL Studio vary the block size). */
    LinkResult simulateLink (double pluginRate, int pluginBlock, double appRate, int appBlock,
                             double seconds, double drift = 0.0, int linkBuffer = 1024)
    {
        const auto shmName = uniqueName();
        SendEndpoint send (shmName);
        AppEndpoint app (shmName);
        send.tick (0.0);
        app.setTargetFill (linkBuffer);
        app.prepare (appRate, appBlock);

        juce::Random rng (42);
        const auto maxBlock = pluginBlock > 0 ? pluginBlock : -pluginBlock;
        std::vector<float> block ((size_t) maxBlock);
        double phase = 0.0, tPlugin = 0.0, tApp = 0.0013;
        bool started = false;
        LinkResult r;

        while (tApp < seconds)
        {
            if (tPlugin <= tApp)
            {
                const auto n = pluginBlock > 0 ? pluginBlock : 1 + rng.nextInt (maxBlock);
                for (int i = 0; i < n; ++i)
                {
                    block[(size_t) i] = (float) (0.5 * std::sin (phase));
                    phase += 2.0 * pi * 220.0 / pluginRate;
                }
                const float* ch[] { block.data() };
                send.writeAudio (ch, 1, n, pluginRate, false);
                tPlugin += n / (pluginRate * (1.0 + drift));
            }
            else
            {
                SourceInputs inputs {};
                app.pullAudio (inputs, appBlock);
                tApp += appBlock / appRate;

                float peak = 0.0f;
                if (inputs[0].numChannels > 0)
                    for (int i = 0; i < appBlock; ++i)
                        peak = std::max (peak, std::abs (inputs[0].channels[0][i]));

                if (peak > 0.1f)        started = true;
                else if (started)       ++r.gaps;
            }
        }

        r.underruns = app.getStatus (0).underruns;
        r.target = app.getStatus (0).targetFrames;
        return r;
    }
}

class IpcTests final : public juce::UnitTest
{
public:
    IpcTests() : juce::UnitTest ("IPC", "Locus") {}

    void runTest() override
    {
        beginTest ("Ring buffer: 1,000,000 frames across two threads arrive intact");
        {
            std::atomic<uint64_t> w { 0 }, r { 0 };
            std::vector<float> left (4096), right (4096);
            AudioRing producerView (w, r, left.data(), right.data(), 4096);
            AudioRing consumerView (w, r, left.data(), right.data(), 4096);
            constexpr int total = 1000000;

            std::thread producer ([&]
            {
                juce::Random rng (1);
                std::vector<float> a (700), b (700);
                for (int written = 0; written < total;)
                {
                    const auto n = std::min (1 + rng.nextInt (700), total - written);
                    for (int i = 0; i < n; ++i)
                    {
                        a[(size_t) i] = (float) (written + i);
                        b[(size_t) i] = -(float) (written + i);
                    }
                    const float* ch[] { a.data(), b.data() };
                    while (! producerView.write (ch, 2, n))
                        std::this_thread::yield();
                    written += n;
                }
            });

            juce::Random rng (2);
            std::vector<float> a (900), b (900);
            int received = 0, errors = 0;
            while (received < total)
            {
                const auto n = std::min (consumerView.getNumReady(), 1 + rng.nextInt (900));
                if (n <= 0) { std::this_thread::yield(); continue; }

                consumerView.peek (a.data(), b.data(), n);
                consumerView.advance (n);
                for (int i = 0; i < n; ++i)
                    errors += (a[(size_t) i] != (float) (received + i) || b[(size_t) i] != -(float) (received + i)) ? 1 : 0;
                received += n;
            }

            producer.join();
            expectEquals (errors, 0);
            expectEquals (received, total);
        }

        beginTest ("Seqlock: readers never see a torn snapshot");
        {
            auto block = std::make_unique<PluginParams>();
            std::atomic<bool> done { false };

            std::thread writer ([&]
            {
                for (int k = 1; k <= 200000; ++k)
                {
                    const auto s = block->lock.beginWrite();
                    for (auto& v : block->values)
                        v.store ((float) k, std::memory_order_relaxed);
                    block->lock.endWrite (s);
                }
                done = true;
            });

            int torn = 0, reads = 0;
            float values[numSyncParams];
            while (! done)
            {
                if (block->lock.read ([&] { for (int p = 0; p < numSyncParams; ++p) values[p] = block->values[p].load (std::memory_order_relaxed); }))
                {
                    ++reads;
                    for (int p = 1; p < numSyncParams; ++p)
                        torn += values[p] != values[0] ? 1 : 0;
                }
            }

            writer.join();
            expectEquals (torn, 0);
            expect (reads > 0);
            logMessage ("  " + juce::String (reads) + " consistent snapshots read during 200,000 writes");
        }

        beginTest ("Slots: auto, explicit, in use, release");
        {
            const auto shmName = uniqueName();
            auto a = std::make_unique<SendEndpoint> (shmName);
            SendEndpoint b (shmName);

            a->tick (0.0);
            expectEquals (a->getSlot(), 0);

            b.setRequestedSlot (1); // slot 1 = index 0, taken by a
            b.tick (0.0);
            expectEquals (b.getSlot(), -1);
            expect (b.getStatus() == SendEndpoint::Status::slotInUse);

            b.setRequestedSlot (0); // Auto
            b.tick (0.1);
            expectEquals (b.getSlot(), 1);

            a.reset(); // destructor releases slot index 0
            SendEndpoint c (shmName);
            c.tick (0.2);
            expectEquals (c.getSlot(), 0);
        }

        beginTest ("Slots: a stale owner's slot is taken over, and the old owner moves on");
        {
            const auto shmName = uniqueName();
            SendEndpoint a (shmName), b (shmName);
            a.setRequestedSlot (3);
            a.tick (0.0);
            expectEquals (a.getSlot(), 2);

            b.setRequestedSlot (3);
            b.tick (0.0);
            expectEquals (b.getSlot(), -1); // a is alive
            b.tick (1.5);
            expectEquals (b.getSlot(), -1); // not stale yet (a hasn't ticked, but < 2 s)
            b.tick (2.6);
            expectEquals (b.getSlot(), 2);  // a silent for > 2 s: taken over

            a.setRequestedSlot (0);
            a.tick (3.0);
            expect (a.getSlot() >= 0 && a.getSlot() != 2, "old owner found another slot: " + juce::String (a.getSlot()));
        }

        beginTest ("Status: app not running until the app's heartbeat appears");
        {
            const auto shmName = uniqueName();
            SendEndpoint send (shmName);
            send.tick (0.0);
            expect (send.getStatus() == SendEndpoint::Status::appNotRunning);

            AppEndpoint app (shmName);
            std::vector<std::unique_ptr<SourceParamStore>> owned;
            const auto stores = storePointers (owned);
            app.tick (0.1, stores);
            send.tick (0.2);
            expect (send.getStatus() == SendEndpoint::Status::connected);

            send.tick (1.5); // app silent for over a second
            expect (send.getStatus() == SendEndpoint::Status::appNotRunning);
        }

        beginTest ("Parameters: plugin values reach the app; app edits reach the plugin with gestures");
        {
            const auto shmName = uniqueName();
            SendEndpoint send (shmName);
            AppEndpoint app (shmName);
            std::vector<std::unique_ptr<SourceParamStore>> owned;
            const auto stores = storePointers (owned);

            send.tick (0.0);
            expect (send.needsPublish());
            auto values = defaultValues();
            values[SourceParam::azimuth] = 45.0f;
            send.publishParameters (values.data());
            send.publishName ("Lead Vocal");

            app.tick (0.0, stores);
            expectEquals (stores[0]->get (SourceParam::azimuth), 45.0f);
            expectEquals (app.getStatus (0).name, juce::String ("Lead Vocal"));
            expect (app.getStatus (0).connected);

            // The app user starts dragging azimuth.
            stores[0]->set (SourceParam::azimuth, 90.0f);
            app.sendEdit (0, SourceParam::azimuth, 90.0f, true);

            // A plugin publish made before it saw the edit must not undo the app's value.
            values[SourceParam::distance] = 2.0f;
            send.publishParameters (values.data());
            app.tick (0.05, stores);
            expectEquals (stores[0]->get (SourceParam::azimuth), 90.0f);

            std::vector<SendEndpoint::AppEdit> edits;
            send.readAppEdits ([&] (const SendEndpoint::AppEdit& e) { edits.push_back (e); });
            expectEquals ((int) edits.size(), 1);
            expectEquals (edits[0].param, (int) SourceParam::azimuth);
            expectEquals (edits[0].value, 90.0f);
            expect (edits[0].changed && edits[0].gestureBegins && ! edits[0].gestureEnds);

            values[SourceParam::azimuth] = 90.0f;
            send.publishParameters (values.data());
            app.tick (0.1, stores);
            expectEquals (stores[0]->get (SourceParam::distance), 2.0f); // accepted now

            // Drag ends.
            app.sendEdit (0, SourceParam::azimuth, 90.0f, false);
            edits.clear();
            send.readAppEdits ([&] (const SendEndpoint::AppEdit& e) { edits.push_back (e); });
            expectEquals ((int) edits.size(), 1);
            expect (! edits[0].gestureBegins && edits[0].gestureEnds);

            // A single click-edit without a drag: begin, set, end.
            app.sendEdit (0, SourceParam::distance, 3.0f, false);
            edits.clear();
            send.readAppEdits ([&] (const SendEndpoint::AppEdit& e) { edits.push_back (e); });
            expectEquals ((int) edits.size(), 1);
            expect (edits[0].changed && edits[0].gestureBegins && edits[0].gestureEnds);

            // Nothing new: nothing applied.
            edits.clear();
            send.readAppEdits ([&] (const SendEndpoint::AppEdit& e) { edits.push_back (e); });
            expectEquals ((int) edits.size(), 0);
        }

        beginTest ("Parameters: edits sent to an empty slot are not applied by its next owner");
        {
            const auto shmName = uniqueName();
            AppEndpoint app (shmName);
            app.sendEdit (4, SourceParam::width, 120.0f, false);

            SendEndpoint send (shmName);
            send.setRequestedSlot (5);
            send.tick (0.0);
            expectEquals (send.getSlot(), 4);

            int count = 0;
            send.readAppEdits ([&] (const SendEndpoint::AppEdit&) { ++count; });
            expectEquals (count, 0);
        }

        beginTest ("Audio: same sample rate passes through cleanly");
        {
            const auto shmName = uniqueName();
            SendEndpoint send (shmName);
            AppEndpoint app (shmName);
            send.tick (0.0);
            app.setTargetFill (1024);
            app.prepare (48000.0, 256);

            std::vector<float> sine (256);
            double phase = 0.0;
            double sumIn = 0.0, sumOut = 0.0;
            int counted = 0;

            for (int block = 0; block < 400; ++block)
            {
                for (auto& x : sine)
                {
                    x = (float) (0.5 * std::sin (phase));
                    phase += 2.0 * pi * 1000.0 / 48000.0;
                }
                const float* ch[] { sine.data() };
                send.writeAudio (ch, 1, 256, 48000.0, false);

                SourceInputs inputs {};
                app.pullAudio (inputs, 256);

                if (block > 100)
                {
                    expectEquals (inputs[0].numChannels, 1);
                    for (int i = 0; i < 256; ++i)
                    {
                        sumIn += (double) sine[(size_t) i] * sine[(size_t) i];
                        sumOut += (double) inputs[0].channels[0][i] * inputs[0].channels[0][i];
                    }
                    ++counted;
                }
            }

            expectWithinAbsoluteError (std::sqrt (sumOut / sumIn), 1.0, 0.01);
            expectEquals (app.getStatus (0).underruns, 0);
            expect (app.getStatus (0).active);
            expectEquals (app.getStatus (1).numChannels, 0);
        }

        beginTest ("Audio: DAW with large blocks (2048 at 44.1 kHz) into a 48 kHz / 512 app, default link buffer");
        {
            // The user's Bitwig setup: the plugin hands over 2048 frames at a time, more than the
            // default 1024-frame target. The app must adapt instead of running dry between blocks.
            const auto shmName = uniqueName();
            SendEndpoint send (shmName);
            AppEndpoint app (shmName);
            send.tick (0.0);
            app.setTargetFill (1024);
            app.prepare (48000.0, 512);

            const double pluginPeriod = 2048.0 / 44100.0, appPeriod = 512.0 / 48000.0;
            double tPlugin = 0.0, tApp = 0.003;
            std::vector<float> block (2048);
            double phase = 0.0;
            int silentBlocksAfterStart = 0;
            bool started = false;

            while (tApp < 30.0)
            {
                if (tPlugin <= tApp)
                {
                    for (auto& x : block)
                    {
                        x = (float) (0.5 * std::sin (phase));
                        phase += 2.0 * pi * 440.0 / 44100.0;
                    }
                    const float* ch[] { block.data() };
                    send.writeAudio (ch, 1, 2048, 44100.0, false);
                    tPlugin += pluginPeriod;
                }
                else
                {
                    SourceInputs inputs {};
                    app.pullAudio (inputs, 512);
                    tApp += appPeriod;

                    float peak = 0.0f;
                    if (inputs[0].numChannels > 0)
                        for (int i = 0; i < 512; ++i)
                            peak = std::max (peak, std::abs (inputs[0].channels[0][i]));

                    if (peak > 0.1f)
                        started = true;
                    else if (started)
                        ++silentBlocksAfterStart; // a gap in a continuous sine = audible dropout
                }
            }

            logMessage ("  underruns " + juce::String (app.getStatus (0).underruns)
                        + ", silent blocks after start " + juce::String (silentBlocksAfterStart)
                        + ", buffering adapted to " + juce::String (app.getStatus (0).targetFrames) + " frames ("
                        + juce::String (app.getStatus (0).targetFrames / 44.1, 1) + " ms)");
            expectEquals (app.getStatus (0).underruns, 0);
            expectEquals (silentBlocksAfterStart, 0);
        }

        beginTest ("Audio: every host block size from 32 to 8192, and random sizes");
        for (const auto block : { 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, -2048, -4096 })
        {
            const auto r = simulateLink (48000.0, block, 48000.0, 512, 20.0);
            const auto label = block > 0 ? juce::String (block) : "random 1.." + juce::String (-block);
            logMessage ("  block " + label.paddedRight (' ', 14) + "underruns " + juce::String (r.underruns)
                        + ", gaps " + juce::String (r.gaps) + ", buffering " + juce::String (r.target));
            expect (r.underruns == 0 && r.gaps == 0, "block " + label);
        }

        beginTest ("Audio: sample-rate combinations (plugin x app), with 0.02 % drift");
        for (const auto pluginRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (const auto appRate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto r = simulateLink (pluginRate, 512, appRate, 512, 15.0, 0.0002);
                const auto label = juce::String (pluginRate / 1000.0, 1) + " -> " + juce::String (appRate / 1000.0, 1) + " kHz";
                if (r.underruns != 0 || r.gaps != 0)
                    logMessage ("  " + label + ": underruns " + juce::String (r.underruns) + ", gaps " + juce::String (r.gaps));
                expect (r.underruns == 0 && r.gaps == 0, label);
            }

        beginTest ("Audio: app with a large device buffer (2048) and a small-block DAW (64)");
        {
            const auto r = simulateLink (48000.0, 64, 48000.0, 2048, 20.0);
            expect (r.underruns == 0 && r.gaps == 0, "underruns " + juce::String (r.underruns) + ", gaps " + juce::String (r.gaps));
        }

        beginTest ("Audio: 10 minutes with 0.1 % drift stays glitch-free");
        {
            const auto r = simulateLink (44100.0, 1024, 48000.0, 480, 600.0, 0.001);
            logMessage ("  underruns " + juce::String (r.underruns) + ", gaps " + juce::String (r.gaps));
            expect (r.underruns == 0 && r.gaps == 0);
        }

        beginTest ("Audio: 48 kHz plugin into a 44.1 kHz app with 0.05 % clock drift, 30 s");
        {
            const auto shmName = uniqueName();
            SendEndpoint send (shmName);
            AppEndpoint app (shmName);
            send.tick (0.0);
            app.setTargetFill (1024);
            app.prepare (44100.0, 441);

            // The plugin's clock runs 0.05 % fast relative to the app's.
            const double pluginPeriod = 480.0 / (48000.0 * 1.0005), appPeriod = 441.0 / 44100.0;
            double tPlugin = 0.0, tApp = 0.005;
            std::vector<float> block (480);
            double phase = 0.0;
            int minFill = 1 << 30, maxFill = 0;

            while (tApp < 30.0)
            {
                if (tPlugin <= tApp)
                {
                    for (auto& x : block)
                    {
                        x = (float) (0.5 * std::sin (phase));
                        phase += 2.0 * pi * 440.0 / 48000.0;
                    }
                    const float* ch[] { block.data() };
                    send.writeAudio (ch, 1, 480, 48000.0, false);
                    tPlugin += pluginPeriod;
                }
                else
                {
                    SourceInputs inputs {};
                    app.pullAudio (inputs, 441);
                    tApp += appPeriod;

                    if (tApp > 10.0) // after the controller has settled
                    {
                        const auto fill = app.getStatus (0).bufferedFrames;
                        minFill = std::min (minFill, fill);
                        maxFill = std::max (maxFill, fill);
                    }
                }
            }

            logMessage ("  underruns " + juce::String (app.getStatus (0).underruns) + ", buffer fill after settling "
                        + juce::String (minFill) + ".." + juce::String (maxFill) + " frames (target 1024)");
            expectEquals (app.getStatus (0).underruns, 0);
            expect (app.getStatus (0).active);
            expect (minFill > 512 && maxFill < 2048, "fill stays near the target");
        }
    }
};

static IpcTests ipcTests;
