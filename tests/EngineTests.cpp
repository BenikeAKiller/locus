#include <dsp/RenderEngine.h>

using namespace locus;

namespace
{
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;

    /** Drives a RenderEngine with one mono source (slot 0) and captures every output. */
    struct Harness
    {
        RenderEngine engine;
        int numOutputs;
        std::vector<std::vector<float>> captured;

        Harness (const SpeakerLayout& layout, bool align = false, int outputs = 8)
            : numOutputs (outputs), captured ((size_t) outputs)
        {
            engine.prepare (fs, blockSize);
            engine.setLayout (layout, align);
        }

        SourceParamStore& source() { return engine.getSourceParams (0); }
        RoomParamStore& room()     { return engine.getRoomParams(); }

        /** Only the direct sound, no air absorption. */
        void dryOnly()
        {
            room().set (RoomParam::earlyOn, 0.0f);
            room().set (RoomParam::clusterOn, 0.0f);
            room().set (RoomParam::reverbOn, 0.0f);
            source().set (SourceParam::air, 0.0f);
        }

        /** Renders the given mono signal (source 0 active throughout) and appends the output. */
        void render (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> (blockSize));
            std::vector<float*> ptrs;
            for (auto& o : out)
                ptrs.push_back (o.data());

            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
                SourceInputs inputs {};
                inputs[0].channels[0] = input.data() + start;
                inputs[0].numChannels = 1;

                engine.process (inputs, ptrs.data(), numOutputs, n);
                engine.releaseRetiredLayouts();

                for (int ch = 0; ch < numOutputs; ++ch)
                    captured[(size_t) ch].insert (captured[(size_t) ch].end(), out[(size_t) ch].begin(), out[(size_t) ch].begin() + n);
            }
        }

        void renderSilence (int numSamples) { render (std::vector<float> ((size_t) numSamples, 0.0f)); }

        void clearCapture() { for (auto& c : captured) c.clear(); }

        float peak (int ch) const
        {
            float p = 0.0f;
            for (auto x : captured[(size_t) ch])
                p = std::max (p, std::abs (x));
            return p;
        }

        int argPeak (int ch) const
        {
            const auto& c = captured[(size_t) ch];
            int best = 0;
            for (int i = 0; i < (int) c.size(); ++i)
                if (std::abs (c[(size_t) i]) > std::abs (c[(size_t) best]))
                    best = i;
            return best;
        }
    };

    std::vector<float> impulse (int length, float amplitude = 0.5f)
    {
        std::vector<float> v ((size_t) length, 0.0f);
        v[0] = amplitude;
        return v;
    }
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Render engine", "Locus") {}

    void runTest() override
    {
        beginTest ("Direct sound reaches only the speaker it is panned to");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.dryOnly();
            h.source().set (SourceParam::azimuth, 30.0f); // R = output 2
            h.renderSilence (4800);                        // let the setup fade in
            h.clearCapture();
            h.render (impulse (2000));

            expectWithinAbsoluteError (h.peak (1), 0.5f, 1.0e-4f);
            expectEquals (h.argPeak (1), 0);
            for (int ch : { 0, 2, 3, 4, 5, 6, 7 })
                expect (h.peak (ch) < 1.0e-6f, "channel " + juce::String (ch + 1));
        }

        beginTest ("Speakers map to their output channels (5.0 leaves channel 4 empty)");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.dryOnly();
            for (auto [az, ch] : { std::pair { 0.0f, 2 }, std::pair { -110.0f, 4 }, std::pair { 110.0f, 5 }, std::pair { -30.0f, 0 } })
            {
                h.source().set (SourceParam::azimuth, az);
                h.renderSilence (4800);
                h.clearCapture();
                h.render (impulse (2000));
                expectWithinAbsoluteError (h.peak (ch), 0.5f, 1.0e-4f, "azimuth " + juce::String (az));
                expect (h.peak (3) < 1.0e-6f);
            }
        }

        beginTest ("Distance gain in the engine: 4 m with Drop 6 dB (Log) is -12 dB");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.dryOnly();
            h.source().set (SourceParam::azimuth, 30.0f);
            h.source().set (SourceParam::distance, 4.0f);
            h.renderSilence (4800);
            h.clearCapture();
            h.render (impulse (2000));
            expectWithinAbsoluteError (h.peak (1), 0.5f * (float) dbToGain (-12.0), 1.0e-3f);
        }

        beginTest ("Doppler delays the direct sound by distance / 343 m/s");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.dryOnly();
            h.source().set (SourceParam::azimuth, 30.0f);
            h.source().set (SourceParam::distance, 10.0f);
            h.source().set (SourceParam::doppler, 1.0f);
            h.renderSilence (4800);
            h.clearCapture();
            h.render (impulse (4000));

            const auto expected = 10.0 / speedOfSound * fs; // 1399.4 samples
            expectWithinAbsoluteError ((double) h.argPeak (1), expected, 1.5);
        }

        beginTest ("Align delays and attenuates the nearer speaker");
        {
            SpeakerLayout layout;
            layout.speakers = { { "Near", 0.0, 0.0, 2.0, 1 }, { "Far", 180.0, 0.0, 4.0, 2 } };
            Harness h (layout, true);
            h.dryOnly();
            h.renderSilence (4800);
            h.clearCapture();
            h.render (impulse (2000));

            expectEquals (h.argPeak (0), (int) std::lround (2.0 / speedOfSound * fs)); // 280 samples
            expectWithinAbsoluteError (h.peak (0), 0.5f * (float) dbToGain (-6.0206), 1.0e-3f);
        }

        beginTest ("Late reverb decays at the set Decay time");
        for (double decay : { 0.5, 1.5 })
        {
            Harness h (*makePresetLayout ("7.0.4"), false, 16);
            h.room().set (RoomParam::earlyOn, 0.0f);
            h.room().set (RoomParam::clusterOn, 0.0f);
            h.room().set (RoomParam::air, 0.0f);
            h.room().set (RoomParam::decay, (float) decay);
            h.room().set (RoomParam::decayLow, 1.0f);
            h.room().set (RoomParam::decayHigh, 1.0f);
            h.source().set (SourceParam::axisMute, 1.0f); // late reverb only
            h.renderSilence (4800);
            h.clearCapture();
            h.render (impulse ((int) (fs * (decay * 1.6 + 0.3))));

            // Schroeder backward integration of the summed output energy.
            const auto length = h.captured[0].size();
            std::vector<double> edc (length + 1, 0.0);
            for (size_t i = length; i-- > 0;)
            {
                double e = 0.0;
                for (const auto& ch : h.captured)
                    e += (double) ch[i] * ch[i];
                edc[i] = edc[i + 1] + e;
            }

            auto crossing = [&] (double db)
            {
                for (size_t i = 0; i < length; ++i)
                    if (10.0 * std::log10 (edc[i] / edc[0] + 1.0e-30) <= db)
                        return (double) i / fs;
                return (double) length / fs;
            };

            const auto t60 = 3.0 * (crossing (-25.0) - crossing (-5.0));
            logMessage ("  decay " + juce::String (decay) + " s -> measured T60 " + juce::String (t60, 3) + " s");
            expectWithinAbsoluteError (t60, decay, decay * 0.15);
        }

        beginTest ("Limiter keeps every output under -0.1 dBFS");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.dryOnly();
            h.source().set (SourceParam::azimuth, 30.0f);
            h.renderSilence (4800);
            h.clearCapture();

            std::vector<float> loud (9600);
            for (size_t i = 0; i < loud.size(); ++i)
                loud[i] = 4.0f * (float) std::sin (2.0 * pi * 440.0 * (double) i / fs); // +12 dBFS

            h.render (loud);
            expect (h.peak (1) <= (float) dbToGain (-0.1) + 1.0e-6f, juce::String (h.peak (1)));

            h.engine.getOutputParams().set (OutputParam::limiter, 0.0f);
            h.clearCapture();
            h.render (loud);
            expect (h.peak (1) > 3.9f, "limiter off passes the signal through");
        }

        beginTest ("Room stages produce output after the direct sound");
        {
            Harness h (*makePresetLayout ("5.0"));
            h.source().set (SourceParam::axisMute, 1.0f);
            h.room().set (RoomParam::reverbOn, 0.0f);
            h.room().set (RoomParam::clusterOn, 0.0f);
            h.renderSilence (4800);
            h.clearCapture();
            h.render (impulse (4800));

            // Early reflections start at 20 ms (at Room Size 2500, Offset 0) and end by 40 ms.
            int first = -1, last = -1;
            for (int i = 0; i < 4800; ++i)
                for (int ch = 0; ch < 8; ++ch)
                    if (std::abs (h.captured[(size_t) ch][(size_t) i]) > 1.0e-6f)
                    {
                        if (first < 0) first = i;
                        last = i;
                    }

            expect (first >= (int) (0.019 * fs) && first <= (int) (0.025 * fs), "first early tap at " + juce::String (first));
            expect (last <= (int) (0.041 * fs), "last early tap at " + juce::String (last));
        }

        beginTest ("Stress: random parameters, setups, panning and HD stay finite and limited");
        {
            juce::Random rng (1234);
            RenderEngine engine;
            engine.prepare (fs, 512);
            engine.setLayout (*makePresetLayout ("7.0.4"), true);

            constexpr int numOutputs = 16;
            std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> (512));
            std::vector<float*> ptrs;
            for (auto& o : out)
                ptrs.push_back (o.data());

            std::vector<std::vector<float>> in ((size_t) maxSources * 2, std::vector<float> (512));
            const auto& presets = getSpeakerPresets();
            bool allFinite = true;
            float maxOut = 0.0f;

            for (int block = 0; block < 400; ++block)
            {
                const auto n = 64 + rng.nextInt (449); // 64..512, not chunk-aligned

                if (block % 40 == 0)
                    engine.setLayout (*makePresetLayout (presets[(size_t) rng.nextInt ((int) presets.size())].name), rng.nextBool());

                if (block % 10 == 0)
                {
                    for (int s = 0; s < maxSources; ++s)
                        for (int p = 0; p < SourceParam::count; ++p)
                        {
                            const auto& spec = getSourceParamSpecs()[(size_t) p];
                            engine.getSourceParams (s).set (p, spec.minValue + rng.nextFloat() * (spec.maxValue - spec.minValue));
                        }

                    for (int p = 0; p < RoomParam::count; ++p)
                    {
                        const auto& spec = getRoomParamSpecs()[(size_t) p];
                        engine.getRoomParams().set (p, spec.minValue + rng.nextFloat() * (spec.maxValue - spec.minValue));
                    }
                }

                SourceInputs inputs {};
                for (int s = 0; s < maxSources; ++s)
                {
                    const auto numCh = rng.nextInt (3); // 0 = silent, 1 = mono, 2 = stereo
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i)
                            in[(size_t) (s * 2 + c)][(size_t) i] = rng.nextFloat() * 2.0f - 1.0f;

                    inputs[(size_t) s].numChannels = numCh;
                    inputs[(size_t) s].channels = { in[(size_t) (s * 2)].data(), in[(size_t) (s * 2 + 1)].data() };
                }

                engine.process (inputs, ptrs.data(), numOutputs, n);
                engine.releaseRetiredLayouts();

                for (const auto& o : out)
                    for (int i = 0; i < n; ++i)
                    {
                        allFinite = allFinite && std::isfinite (o[(size_t) i]);
                        maxOut = std::max (maxOut, std::abs (o[(size_t) i]));
                    }
            }

            expect (allFinite, "non-finite output");
            expect (maxOut <= (float) dbToGain (-0.1) + 1.0e-6f, "peak " + juce::String (maxOut));
        }

        beginTest ("Default room balance (logged)");
        {
            auto totalRms = [] (const Harness& h, size_t from)
            {
                double sum = 0.0;
                size_t count = 0;
                for (const auto& ch : h.captured)
                {
                    for (size_t i = from; i < ch.size(); ++i)
                        sum += (double) ch[i] * ch[i];
                    count = ch.size() - from;
                }
                return std::sqrt (sum / (double) count);
            };

            juce::Random rng (7);
            std::vector<float> noise ((size_t) (fs * 4.0));
            for (auto& x : noise)
                x = 0.125f * (rng.nextFloat() * 2.0f - 1.0f);

            double levels[4] {};
            const char* names[4] { "direct", "early", "cluster", "reverb" };

            for (int stage = 0; stage < 4; ++stage)
            {
                Harness h (*makePresetLayout ("5.0"));
                h.source().set (SourceParam::azimuth, 30.0f);
                h.source().set (SourceParam::axisMute, stage == 0 ? 0.0f : 1.0f);
                h.room().set (RoomParam::earlyOn, stage == 1 ? 1.0f : 0.0f);
                h.room().set (RoomParam::clusterOn, stage == 2 ? 1.0f : 0.0f);
                h.room().set (RoomParam::reverbOn, stage == 3 ? 1.0f : 0.0f);
                h.render (noise);
                levels[stage] = gainToDb (totalRms (h, (size_t) (fs * 3.0))); // steady state: last second
            }

            for (int stage = 0; stage < 4; ++stage)
                logMessage ("  " + juce::String (names[stage]).paddedRight (' ', 8) + juce::String (levels[stage], 1)
                            + " dB  (" + juce::String (levels[stage] - levels[0], 1) + " dB re direct)");
        }

        beginTest ("App debug test signal, rendered offline (logged)");
        {
            Harness h (*makePresetLayout ("5.0"), true);
            juce::Random rng (3);
            const auto burst = (int) (0.25 * fs), period = (int) (0.5 * fs);

            for (int second = 0; second < 10; ++second)
            {
                h.clearCapture();
                for (int block = 0; block < (int) fs / blockSize; ++block)
                {
                    const auto pos = second * (int) fs + block * blockSize;
                    h.source().set (SourceParam::azimuth, (float) wrapAzimuth (pos / fs * 45.0));

                    std::vector<float> sig ((size_t) blockSize);
                    for (int i = 0; i < blockSize; ++i)
                        sig[(size_t) i] = (pos + i) % period < burst ? 0.125f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
                    h.render (sig);
                }

                juce::String line ("  t=" + juce::String (second) + "-" + juce::String (second + 1) + " s, az "
                                   + juce::String ((int) wrapAzimuth (second * 45.0)) + ".." + juce::String ((int) wrapAzimuth ((second + 1) * 45.0)) + ":");
                for (int ch : { 0, 1, 2, 4, 5 })
                    line << "  ch" << (ch + 1) << " " << juce::String (h.peak (ch), 3);
                logMessage (line);
            }
        }

        beginTest ("Performance: 16 stereo sources, 7.0.4, HD off, 48 kHz / 256 (logged)");
        for (auto moving : { false, true })
        for (auto panning : { PanningChoice::angular, PanningChoice::ambisonics })
        {
            RenderEngine engine;
            engine.prepare (fs, blockSize);
            engine.setLayout (*makePresetLayout ("7.0.4"), true);

            constexpr int numOutputs = 12;
            std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> (blockSize));
            std::vector<float*> ptrs;
            for (auto& o : out)
                ptrs.push_back (o.data());

            juce::Random rng (99);
            std::vector<float> noise ((size_t) blockSize * 2);
            for (auto& x : noise)
                x = rng.nextFloat() * 0.2f - 0.1f;

            SourceInputs inputs {};
            for (int s = 0; s < maxSources; ++s)
            {
                inputs[(size_t) s] = { { noise.data(), noise.data() + blockSize }, 2 };
                engine.getSourceParams (s).set (SourceParam::panning, (float) panning);
                engine.getSourceParams (s).set (SourceParam::doppler, 1.0f);
            }

            const int numBlocks = (int) (5.0 * fs / blockSize); // 5 s of audio
            const auto start = juce::Time::getHighResolutionTicks();

            for (int b = 0; b < numBlocks; ++b)
            {
                // Moving: every source changes every block, so panning gains are recomputed constantly.
                if (moving || b == 0)
                    for (int s = 0; s < maxSources; ++s)
                        engine.getSourceParams (s).set (SourceParam::azimuth, (float) wrapAzimuth (b * 2.0 + s * 22.5));

                engine.process (inputs, ptrs.data(), numOutputs, blockSize);
            }

            const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            const auto load = 100.0 * seconds / 5.0;
            logMessage ("  " + juce::String (moving ? "moving " : "static ")
                        + juce::String (panning == PanningChoice::angular ? "Angular   " : "Ambisonics")
                        + ": " + juce::String (load, 1) + " % of one core (target < 15 %)");
            expect (load < 100.0, "slower than real time");
        }
    }
};

static EngineTests engineTests;
