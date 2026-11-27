#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

namespace locus
{
    enum class ParamType
    {
        continuous,
        toggle, // 0 = off, 1 = on
        choice  // 0 .. choices.size() - 1
    };

    /** One row of a parameter table (spec §6 and §7.4). The Send plugin's automatable
        parameters, the shared-memory parameter block and the app UI are all generated
        from these tables, so the order and ids must stay stable. */
    struct ParamSpec
    {
        juce::String id;
        juce::String name;
        ParamType type = ParamType::continuous;
        float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
        juce::String unit;
        juce::StringArray choices;
        float midpoint = 0.0f; // value at a slider's centre for log-like ranges; 0 = linear

        float clamp (float v) const noexcept { return juce::jlimit (minValue, maxValue, v); }
    };

    namespace SourceParam
    {
        enum Index : int
        {
            azimuth, distance, elevation, doppler, orientation, aperture, width, spread, panRev,
            panning, air, airFreq, nearRadius, dropMode, drop,
            axisLow, axisMid, axisHigh, axisFreqLow, axisFreqHigh, axisGain, axisBypass, axisMute,
            omniLow, omniMid, omniHigh, omniFreqLow, omniFreqHigh, omniGain, omniBypass, omniMute,
            count
        };
    }

    namespace RoomParam
    {
        enum Index : int
        {
            roomSize, offset, earlyShape, decay, infinite, decayLow, decayHigh, freqLow, freqHigh,
            hd, air, airFreq, earlyLevel, clusterLevel, reverbLevel, earlyOn, clusterOn, reverbOn,
            count
        };
    }

    namespace OutputParam
    {
        enum Index : int { volume, limiter, count };
    }

    /** Values of the "panning" choice parameter. */
    enum class PanningChoice : int { angular = 0, nearest = 1, ambisonics = 2 };

    /** Values of the "dropMode" choice parameter. */
    enum class DropMode : int { linear = 0, logarithmic = 1 };

    const std::vector<ParamSpec>& getSourceParamSpecs();
    const std::vector<ParamSpec>& getRoomParamSpecs();
    const std::vector<ParamSpec>& getOutputParamSpecs();

    /** Lock-free parameter values, written by the message thread (UI, IPC) and read
        by the audio thread once per block. */
    template <int NumParams>
    class ParamStore
    {
    public:
        explicit ParamStore (const std::vector<ParamSpec>& specsToUse) : specs (specsToUse)
        {
            jassert ((int) specs.size() == NumParams);
            resetToDefaults();
        }

        float get (int index) const noexcept           { return values[(size_t) index].load (std::memory_order_relaxed); }
        bool getBool (int index) const noexcept        { return get (index) >= 0.5f; }
        int getChoice (int index) const noexcept       { return (int) std::lround (get (index)); }
        void set (int index, float v) noexcept         { values[(size_t) index].store (specs[(size_t) index].clamp (v), std::memory_order_relaxed); }

        void resetToDefaults() noexcept
        {
            for (int i = 0; i < NumParams; ++i)
                values[(size_t) i].store (specs[(size_t) i].defaultValue, std::memory_order_relaxed);
        }

        const std::vector<ParamSpec>& getSpecs() const noexcept { return specs; }

    private:
        const std::vector<ParamSpec>& specs;
        std::array<std::atomic<float>, (size_t) NumParams> values;
    };

    using SourceParamStore = ParamStore<SourceParam::count>;
    using RoomParamStore   = ParamStore<RoomParam::count>;
    using OutputParamStore = ParamStore<OutputParam::count>;
}
