#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SharedMemory.h"
#include "SharedLayout.h"
#include "dsp/RenderTypes.h"

namespace locus::ipc
{
    /** The app's side of the shared memory (spec §8): reads every slot's audio with drift
        compensation, keeps the engine's source parameters in sync with the Send plugins, and
        sends the app's own edits back.

        Threading: prepare() and pullAudio() belong to the audio thread (prepare() only while
        audio is stopped); tick(), sendEdit() and getStatus() to the message thread.
    */
    class AppEndpoint
    {
    public:
        struct SlotStatus
        {
            bool connected = false; // a Send plugin owns the slot and is alive
            bool active = false;    // its audio is arriving
            juce::String name;
            int numChannels = 0;
            int underruns = 0;
            int bufferedFrames = 0; // frames waiting in the ring
            int targetFrames = 0;   // buffering in use, adapted to the plugin's block size
        };

        explicit AppEndpoint (const juce::String& sharedMemoryName = defaultSharedMemoryName);

        bool isOpen() const noexcept { return layout != nullptr; }

        //==============================================================================
        /** Target buffering per slot, in frames at the plugin's rate (spec: 256..2048, default 1024). */
        void setTargetFill (int frames) noexcept { targetFill.store (juce::jlimit (64, ringFrames / 2, frames)); }

        void prepare (double sampleRate, int maxBlockSize);

        /** Fills one SourceInput per slot for this block (numChannels 0 when idle). */
        void pullAudio (SourceInputs& inputs, int numSamples) noexcept;

        //==============================================================================
        /** Call regularly (~30 Hz): app heartbeat, plugin parameters into the stores, names. */
        void tick (double nowSeconds, const std::array<SourceParamStore*, maxSources>& stores);

        /** Sends one app-side edit to the slot's plugin. gestureActive: the user is still dragging. */
        void sendEdit (int slot, int param, float value, bool gestureActive);

        SlotStatus getStatus (int slot) const;

        /** Real-time safe: true if any Send plugin holds the slot (alive or not). */
        bool isSlotOwned (int slot) const noexcept
        {
            return layout != nullptr && layout->slots[slot].owner.load (std::memory_order_relaxed) != 0;
        }

        /** Seconds without audio after which a slot counts as idle (spec: 200 ms). */
        static constexpr double activityTimeoutSeconds = 0.2;

    private:
        struct AudioState
        {
            uint32_t lastAudioBeat = 0;
            int silentSamples = 1 << 30;
            bool wasActive = false;
            bool priming = true;
            double smoothedFill = 0.0, integral = 0.0, correction = 0.0;
            uint64_t blockWriteIndex = 0;
            uint32_t blockBeat = 0;
            int pluginBlock = 0; // largest block size seen from the plugin since it became active
            std::array<juce::LagrangeInterpolator, 2> interpolators;
            std::array<std::vector<float>, 2> output, scratch;
        };

        void pullSlot (int index, SourceInput& input, int numSamples) noexcept;

        SharedMemory memory;
        SharedLayout* layout = nullptr;

        // Audio thread
        double sampleRate = 48000.0;
        int maxBlock = 0;
        std::array<AudioState, maxSources> audio;
        std::atomic<int> targetFill { 1024 };
        std::array<std::atomic<bool>, maxSources> active {};
        std::array<std::atomic<int>, maxSources> underruns {}, channels {}, lastTarget {};

        // Message thread
        std::array<uint64_t, maxSources> knownOwner {};
        std::array<uint32_t, maxSources> lastPluginSequence {}, lastAppWrite {}, seenAlive {};
        std::array<double, maxSources> seenAliveTime {};
        std::array<bool, maxSources> alive {};
        std::array<juce::String, maxSources> names;
        double now = 0.0;
    };
}
