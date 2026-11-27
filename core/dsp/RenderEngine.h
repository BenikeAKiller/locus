#pragma once

#include <juce_core/juce_core.h>

#include "LateReverb.h"
#include "SourceProcessor.h"

namespace locus
{
    /** The whole renderer: 16 sources, the shared room, Align, master volume, limiter
        and output mapping (spec §7).

        Threading: setLayout(), releaseRetiredLayouts() and the parameter stores are used from
        the message thread; prepare() must not run concurrently with process(); process() is
        real-time safe.
    */
    class RenderEngine
    {
    public:
        RenderEngine();
        ~RenderEngine();

        //==============================================================================
        /** Builds the new speaker setup here and hands it to the audio thread, which fades
            out, swaps and fades back in (30 ms total). */
        void setLayout (const SpeakerLayout& layout, bool align);

        /** Frees layouts the audio thread has finished with. Call regularly (e.g. from a timer). */
        void releaseRetiredLayouts();

        SourceParamStore& getSourceParams (int slot) noexcept { return *sourceParams[(size_t) slot]; }
        RoomParamStore& getRoomParams() noexcept              { return roomParams; }
        OutputParamStore& getOutputParams() noexcept          { return outputParams; }

        /** Peak level of a device output channel since the last call. */
        float getAndResetPeak (int deviceChannel) noexcept;

        //==============================================================================
        void prepare (double sampleRate, int maxBlockSize);

        /** Renders numSamples into the device outputs. Unused output channels are zeroed. */
        void process (const SourceInputs& inputs, float* const* outputs, int numOutputs, int numSamples) noexcept;

        /** Seconds a source keeps rendering after its input stops, so room tails finish. */
        static constexpr double sourceTailSeconds = 1.0;

    private:
        struct RoomSmoothers
        {
            Smoother roomSize, offset, earlyShape, decay, decayLow, decayHigh, freqLow, freqHigh, airFreq, airMix;
            Smoother earlyLevel, clusterLevel, reverbLevel;
        };

        void setRoomTargets() noexcept;
        void buildRoomFrame (int numSamples, double hdFade) noexcept;
        void handleLayoutSwap (int numSamples) noexcept;
        void renderOutputs (float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;

        double sampleRate = 48000.0;
        bool prepared = false;

        std::array<std::unique_ptr<SourceParamStore>, maxSources> sourceParams;
        RoomParamStore roomParams { getRoomParamSpecs() };
        OutputParamStore outputParams { getOutputParamSpecs() };

        std::array<SourceProcessor, maxSources> sources;
        std::array<bool, maxSources> sourceRunning {};
        std::array<int, maxSources> tailRemaining {}, lastNumChannels {};

        LateReverb late;
        RoomSmoothers room;
        RoomFrame roomFrame;
        LateReverb::Settings lateSettings;
        bool infinite = false, activeHd = false, requestedHd = false;
        Smoother hdFade;
        float previousLateLevel = 0.0f;

        // Speaker setup handover
        std::atomic<LayoutState*> pendingLayout { nullptr };
        LayoutState* currentLayout = nullptr;
        juce::AbstractFifo retiredFifo { 32 };
        std::array<LayoutState*, 32> retired {};
        Smoother layoutFade;

        // Output stage
        Smoother volume;
        bool limiterOn = true;
        double limiterGain = 1.0, limiterAttack = 0.0, limiterRelease = 0.0;
        std::array<DelayLine, maxSpeakers> alignLines;
        std::array<std::atomic<float>, maxSpeakers> peaks {};

        std::array<std::array<float, renderChunk>, maxSpeakers> busData {};
        std::array<float, renderChunk> lateSend {}, zeros {};
        SpeakerBus bus;
    };
}
