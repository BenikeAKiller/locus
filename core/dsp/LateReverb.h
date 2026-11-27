#pragma once

#include "Biquad.h"
#include "DspUtils.h"
#include "RenderTypes.h"

namespace locus
{
    /** Shared late reverb (spec §7.4): a feedback delay network with 8 lines (16 with HD),
        Hadamard mixing and 3-band absorption per line. Each line's output is panned to a
        fixed virtual direction via the gains in LayoutState::lateGains.

        prepare() allocates; everything else is real-time safe.
    */
    class LateReverb
    {
    public:
        struct Settings
        {
            double roomScale = 1.0;    // cbrt(roomSize / 2500)
            double offsetMs = 0.0;
            double decay = 2.5;        // mid-frequency T60, seconds
            double decayLow = 1.0;     // multipliers
            double decayHigh = 0.5;
            double freqLow = 250.0;
            double freqHigh = 8000.0;
            bool infinite = false;
            double airFreq = 10000.0;
            double airMix = 1.0;       // 0 = air off, 1 = on (smoothed)
        };

        void prepare (double sampleRate);

        /** 8 or 16. Clears the network. */
        void setNumLines (int numLines) noexcept;
        int getNumLines() const noexcept { return numLines; }

        void clear() noexcept;

        /** Runs one chunk of mono input and adds the panned output to the speaker bus.
            lineGains holds getNumLines() x bus.numSpeakers gains. */
        void processChunk (const float* input, int numSamples, const Settings& settings,
                           const float* lineGains, float levelStart, float levelEnd, SpeakerBus& bus) noexcept;

        /** Effective mid-frequency T60 in seconds for the given settings. */
        static double effectiveDecay (const Settings& s) noexcept { return s.infinite ? 1000.0 : s.decay; }

    private:
        void updateLengths (double roomScale) noexcept;
        void updateAbsorption (const Settings& s) noexcept;

        double sampleRate = 48000.0;
        int numLines = 8;

        std::array<DelayLine, maxLateLines> lines;
        std::array<Smoother, maxLateLines> length;
        std::array<Biquad, maxLateLines> lowShelf, highShelf;
        std::array<float, maxLateLines> midGain {};
        std::array<std::array<float, renderChunk>, maxLateLines> lineOut {};

        DelayLine predelay;
        double previousPredelay = 0.0;
        double airState = 0.0;
        double previousAirMix = 1.0;

        double lastScale = -1.0;
        Settings lastAbsorption {};
        bool absorptionValid = false;
    };
}
