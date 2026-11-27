#pragma once

#include <array>
#include <memory>
#include <vector>

#include "Panners.h"
#include "params/Parameters.h"

namespace locus
{
    /** Control rate: smoothed parameters, filter coefficients and panning gains are
        updated once per chunk; gains are interpolated per sample within it. */
    constexpr int renderChunk = 32;

    constexpr int maxEarlyTaps   = 16;
    constexpr int numClusterTaps = 4;
    constexpr int maxLateLines   = 16;

    /** Panning spread used for the room stages (spec §7.4). */
    constexpr double earlyPanSpread   = 0.5;
    constexpr double clusterPanSpread = 0.5;
    constexpr double latePanSpread    = 0.3;

    /** Everything that depends on the speaker setup. Built on the message thread,
        handed to the audio thread, never modified afterwards. */
    struct LayoutState
    {
        SpeakerLayout layout;
        bool align = false;
        int numSpeakers = 0;
        bool threeD = false;

        std::array<std::unique_ptr<Panner>, 3> panners; // indexed by PanningChoice
        std::array<std::vector<float>, 2> lateGains;     // [0]: 8 lines, [1]: 16 lines; lines x speakers
        std::vector<int> deviceChannel;                  // 0-based output channel per speaker
        std::vector<double> alignDelaySeconds;
        std::vector<float> alignGain;

        const Panner& getPanner (int panningChoice) const noexcept
        {
            return *panners[(size_t) std::clamp (panningChoice, 0, 2)];
        }

        static std::unique_ptr<LayoutState> create (const SpeakerLayout& layout, bool align);
    };

    /** Virtual direction of FDN line l of L: spaced round the circle (2D) or over the
        upper hemisphere (3D). */
    Vec3 lateLineDirection (int line, int numLines, bool threeD) noexcept;

    /** Room values for one chunk, shared by every source. */
    struct RoomFrame
    {
        int numEarlyTaps = 8;
        std::array<double, maxEarlyTaps> earlyDelay {};    // samples
        std::array<float, maxEarlyTaps> earlyGain {};
        std::array<double, numClusterTaps> clusterDelay {}; // samples
        float earlyLevel = 1.0f;   // linear: level, on/off and HD fade
        float clusterLevel = 1.0f; // linear, includes the per-tap calibration
    };

    /** Audio for one source for one block. numChannels = 0 means the source is silent/absent. */
    struct SourceInput
    {
        std::array<const float*, 2> channels {};
        int numChannels = 0;
    };

    using SourceInputs = std::array<SourceInput, maxSources>;

    /** Per-speaker accumulation buffers for one chunk. */
    struct SpeakerBus
    {
        std::array<float*, maxSpeakers> channels {};
        int numSpeakers = 0;
    };
}
