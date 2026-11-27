#pragma once

#include "Biquad.h"
#include "DspUtils.h"
#include "RenderTypes.h"

namespace locus
{
    /** One source's chain (spec §7.1): Doppler, then a direct path (air, directivity,
        Axis EQ, distance gain, panner) and a room path (Omni EQ) feeding per-source early
        reflections and cluster, plus a mono send to the shared late reverb.

        prepare() allocates; everything else is real-time safe.
    */
    class SourceProcessor
    {
    public:
        void prepare (double sampleRate);

        /** Reads the parameter values for this block. */
        void setTargets (const SourceParamStore& params) noexcept;

        /** Jumps every smoothed value to its target and clears all audio state.
            Call after setTargets() when a silent source starts again. */
        void restart() noexcept;

        /** Forces new panning gains, e.g. after a speaker setup change. */
        void invalidatePanning() noexcept { panCacheValid = roomPanCacheValid = false; firstChunk = true; }

        /** Processes up to renderChunk samples, adding into the speaker bus and the late send. */
        void processChunk (const float* const* input, int numInputChannels, int numSamples,
                           const LayoutState& layout, const RoomFrame& room,
                           SpeakerBus& bus, float* lateSend) noexcept;

    private:
        using GainVector = std::array<float, maxSpeakers>;

        /** Early/cluster panning is refreshed at most every this many chunks. */
        static constexpr int roomPanInterval = 4;

        struct Bus
        {
            GainVector unit {}, previous {}, current {};
        };

        void updatePanning (const LayoutState& layout, int numChannels, double az, double el,
                            double width, double spread, double panRev) noexcept;
        void updateFilters (double distance, double nearRadius, double airFreq, double orientation, double aperture) noexcept;
        /** Adds signal to every speaker, interpolating from bus.previous to bus.current gains. */
        static void mixBus (const float* signal, Bus& bus, int numSpeakers, int numSamples, SpeakerBus& out) noexcept;

        double sampleRate = 48000.0;

        // Smoothed parameters
        AngleSmoother azimuth, orientation;
        Smoother distance, elevation, aperture, width, spread, panRev, airFreq, nearRadius, drop;
        Smoother axisLow, axisMid, axisHigh, axisFreqLow, axisFreqHigh, axisGain, axisOn;
        Smoother omniLow, omniMid, omniHigh, omniFreqLow, omniFreqHigh, omniGain, omniOn;
        Smoother dopplerMix, airMix, panFade;
        bool logDrop = true;
        int requestedPanning = 0, activePanning = 0;

        // Audio state, per input channel
        std::array<DelayLine, 2> dopplerLine, roomLine;
        std::array<Biquad, 2> airFilter, directivityFilter, axisLowShelf, axisHighShelf, omniLowShelf, omniHighShelf;
        std::array<std::array<DelayLine, 2>, numClusterTaps> clusterAllpass;
        std::array<std::array<int, 2>, numClusterTaps> allpassDelay {};
        double previousDopplerDelay = 1.0;
        float previousRoomGain = 0.0f, previousAirMix = 1.0f, previousDopplerMix = 0.0f;

        // Cached filter designs
        double lastAirCutoff = -1.0, lastDirectivityDb = 1.0e9;
        ShelfEqSettings lastAxis { 1.0e9 }, lastOmni { 1.0e9 };

        // Panning: direct (L, R), early sides (A, B), cluster (4)
        std::array<Bus, 2> directBus, earlyBus;
        std::array<Bus, numClusterTaps> clusterBus;
        bool panCacheValid = false, roomPanCacheValid = false, firstChunk = true;
        std::array<double, 6> panCache {}, roomPanCache {};
        int chunksSinceRoomPan = 0;

        // Per-chunk signal scratch
        std::array<std::array<float, renderChunk>, 2> directSignal {}, earlySignal {};
        std::array<std::array<float, renderChunk>, numClusterTaps> clusterSignal {};
    };
}
