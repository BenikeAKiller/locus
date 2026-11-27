#pragma once

#include "SharedMemory.h"
#include "SharedLayout.h"

namespace locus::ipc
{
    /** The Send plugin's side of the shared memory (spec §8): claims a slot, streams audio into
        it, publishes parameters and receives the app's edits.

        Threading: writeAudio() is for the audio thread and is real-time safe; everything else
        is for the message thread.
    */
    class SendEndpoint
    {
    public:
        enum class Status
        {
            noSharedMemory, // the region could not be opened, or holds another layout version
            appNotRunning,
            connected,
            slotInUse,      // the requested slot (or, with Auto, every slot) is taken
            offline         // the host is rendering offline
        };

        /** One parameter change coming from the app, with the host gesture calls it needs. */
        struct AppEdit
        {
            int param = 0;
            float value = 0.0f;
            bool changed = false;       // call setValueNotifyingHost
            bool gestureBegins = false; // call beginChangeGesture first
            bool gestureEnds = false;   // call endChangeGesture afterwards
        };

        explicit SendEndpoint (const juce::String& sharedMemoryName = defaultSharedMemoryName);
        ~SendEndpoint();

        //==============================================================================
        /** 0 = Auto (first free slot), 1..16 = that slot. */
        void setRequestedSlot (int requested);
        int getRequestedSlot() const noexcept { return requestedSlot; }

        /** Call regularly (~30 Hz): heartbeat, slot (re)acquisition, app detection. */
        void tick (double nowSeconds);

        /** 0-based slot, or -1 when none is held. */
        int getSlot() const noexcept { return slot.load (std::memory_order_acquire); }
        Status getStatus() const noexcept { return status; }

        /** True once after acquiring a slot or applying app edits: publish all parameters. */
        bool needsPublish() const noexcept { return publishRequested; }

        void publishParameters (const float* values);
        void publishName (const juce::String& name);

        /** Reads the app's new edits; fn is called once per affected parameter. */
        template <typename Fn>
        void readAppEdits (Fn&& fn);

        //==============================================================================
        /** Audio thread: hands one block to the app. Silent when no slot is held. */
        void writeAudio (const float* const* channels, int numChannels, int numSamples,
                         double sampleRate, bool nonRealtime) noexcept;

        /** Seconds without the owner's heartbeat after which another plugin may take a slot. */
        static constexpr double staleOwnerSeconds = 2.0;
        /** Seconds without the app's heartbeat after which it counts as not running. */
        static constexpr double appTimeoutSeconds = 1.0;

    private:
        bool tryAcquire (double now);
        void release();
        Slot& ownSlot() const noexcept { return layout->slots[slot.load()]; }

        SharedMemory memory;
        SharedLayout* layout = nullptr;

        const uint64_t instanceId;
        int requestedSlot = 0;
        std::atomic<int> slot { -1 };
        std::atomic<bool> offline { false };
        Status status = Status::noSharedMemory;
        double lastAttempt = -1.0e9;
        bool publishRequested = false;

        uint32_t lastAppBeat = 0;
        double lastAppBeatTime = -1.0e9;

        std::array<uint32_t, maxSources> seenAlive {};
        std::array<uint64_t, maxSources> seenOwner {};
        std::array<double, maxSources> seenTime {};

        uint32_t appliedAppSequence = 0;
        std::array<uint32_t, numSyncParams> appliedChangeCount {};
        uint32_t gestureMask = 0;
    };

    //==============================================================================
    template <typename Fn>
    void SendEndpoint::readAppEdits (Fn&& fn)
    {
        if (slot.load() < 0)
            return;

        auto& edits = ownSlot().fromApp;
        float values[numSyncParams];
        uint32_t counts[numSyncParams];
        uint32_t mask = 0, sequence = 0;

        const auto ok = edits.lock.read ([&]
        {
            for (int p = 0; p < numSyncParams; ++p)
            {
                values[p] = edits.values[p].load (std::memory_order_relaxed);
                counts[p] = edits.changeCount[p].load (std::memory_order_relaxed);
            }
            mask = edits.gestureMask.load (std::memory_order_relaxed);
        }, &sequence);

        if (! ok || sequence == appliedAppSequence)
            return;

        for (int p = 0; p < numSyncParams; ++p)
        {
            const auto bit = 1u << p;
            const bool wasInGesture = (gestureMask & bit) != 0;
            const bool inGesture = (mask & bit) != 0;

            AppEdit e;
            e.param = p;
            e.value = values[p];
            e.changed = counts[p] != appliedChangeCount[(size_t) p];
            e.gestureBegins = ! wasInGesture && (inGesture || e.changed);
            e.gestureEnds = (wasInGesture || e.changed) && ! inGesture;
            appliedChangeCount[(size_t) p] = counts[p];

            if (e.changed || e.gestureBegins || e.gestureEnds)
                fn (e);
        }

        gestureMask = mask;
        appliedAppSequence = sequence;
        publishRequested = true; // tell the app which edits are now applied
    }
}
