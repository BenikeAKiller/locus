#include "SendEndpoint.h"
#include "AudioRing.h"

namespace locus::ipc
{
    namespace
    {
        uint64_t makeInstanceId()
        {
            uint64_t id = 0;
            while (id == 0)
                id = (uint64_t) juce::Random::getSystemRandom().nextInt64();
            return id;
        }
    }

    SendEndpoint::SendEndpoint (const juce::String& sharedMemoryName)
        : instanceId (makeInstanceId())
    {
        if (memory.open (sharedMemoryName, sizeof (SharedLayout)))
        {
            auto* candidate = static_cast<SharedLayout*> (memory.getData());
            if (initialiseLayout (*candidate))
                layout = candidate;
        }

        // Until the first tick() has seen the app's heartbeat, assume it isn't running.
        status = layout != nullptr ? Status::appNotRunning : Status::noSharedMemory;
    }

    SendEndpoint::~SendEndpoint()
    {
        release();
    }

    void SendEndpoint::setRequestedSlot (int requested)
    {
        requested = juce::jlimit (0, maxSources, requested);
        if (requested == requestedSlot)
            return;

        requestedSlot = requested;
        release();
        lastAttempt = -1.0e9; // try again on the next tick
    }

    void SendEndpoint::release()
    {
        const auto s = slot.exchange (-1);
        if (s >= 0 && layout != nullptr)
        {
            auto expected = instanceId;
            layout->slots[s].owner.compare_exchange_strong (expected, 0);
        }
    }

    bool SendEndpoint::tryAcquire (double now)
    {
        auto take = [&] (int i)
        {
            auto& s = layout->slots[i];
            uint64_t expected = 0;
            if (s.owner.compare_exchange_strong (expected, instanceId))
                return true;

            // Take over a slot whose owner stopped its heartbeat (e.g. its host crashed).
            const bool stale = expected == seenOwner[(size_t) i] && now - seenTime[(size_t) i] > staleOwnerSeconds;
            return stale && s.owner.compare_exchange_strong (expected, instanceId);
        };

        int acquired = -1;
        if (requestedSlot > 0)
        {
            if (take (requestedSlot - 1))
                acquired = requestedSlot - 1;
        }
        else
        {
            for (int i = 0; i < maxSources && acquired < 0; ++i)
                if (take (i))
                    acquired = i;
        }

        if (acquired < 0)
            return false;

        auto& s = layout->slots[acquired];
        s.aliveBeat.fetch_add (1);

        // Ignore edits the app made for a previous owner of this slot.
        auto& edits = s.fromApp;
        edits.lock.read ([&]
        {
            for (int p = 0; p < numSyncParams; ++p)
                appliedChangeCount[(size_t) p] = edits.changeCount[p].load (std::memory_order_relaxed);
        }, &appliedAppSequence);
        gestureMask = 0;

        slot.store (acquired, std::memory_order_release);
        publishRequested = true; // the plugin's values are the source of truth on connect
        return true;
    }

    void SendEndpoint::tick (double now)
    {
        if (layout == nullptr)
        {
            status = Status::noSharedMemory;
            return;
        }

        const auto appBeat = layout->header.appHeartbeat.load();
        if (appBeat != lastAppBeat)
        {
            lastAppBeat = appBeat;
            lastAppBeatTime = now;
        }
        const bool appRunning = now - lastAppBeatTime < appTimeoutSeconds;

        for (int i = 0; i < maxSources; ++i)
        {
            const auto alive = layout->slots[i].aliveBeat.load();
            const auto owner = layout->slots[i].owner.load();
            if (alive != seenAlive[(size_t) i] || owner != seenOwner[(size_t) i])
            {
                seenAlive[(size_t) i] = alive;
                seenOwner[(size_t) i] = owner;
                seenTime[(size_t) i] = now;
            }
        }

        if (const auto s = slot.load(); s >= 0)
        {
            if (layout->slots[s].owner.load() == instanceId)
                layout->slots[s].aliveBeat.fetch_add (1);
            else
                slot.store (-1); // taken over after we went quiet; look for a slot again
        }

        if (slot.load() < 0 && now - lastAttempt >= 1.0)
        {
            lastAttempt = now;
            tryAcquire (now);
        }

        if (offline.load())
            status = Status::offline;
        else if (slot.load() < 0)
            status = Status::slotInUse;
        else
            status = appRunning ? Status::connected : Status::appNotRunning;
    }

    void SendEndpoint::publishParameters (const float* values)
    {
        publishRequested = false;

        if (slot.load() < 0)
            return;

        auto& block = ownSlot().fromPlugin;
        const auto s = block.lock.beginWrite();
        for (int p = 0; p < numSyncParams; ++p)
            block.values[p].store (values[p], std::memory_order_relaxed);
        block.appliedAppSequence.store (appliedAppSequence, std::memory_order_relaxed);
        block.publisher.store (instanceId, std::memory_order_relaxed);
        block.lock.endWrite (s);
    }

    void SendEndpoint::publishName (const juce::String& name)
    {
        if (slot.load() >= 0)
            writeName (ownSlot().name, name);
    }

    void SendEndpoint::writeAudio (const float* const* channels, int numChannels, int numSamples,
                                   double sampleRate, bool nonRealtime) noexcept
    {
        offline.store (nonRealtime, std::memory_order_relaxed);

        const auto s = slot.load (std::memory_order_acquire);
        if (s < 0 || numChannels < 1 || numSamples < 1)
            return;

        auto& sl = layout->slots[s];
        sl.nonRealtime.store (nonRealtime ? 1u : 0u, std::memory_order_relaxed);
        if (nonRealtime)
            return;

        sl.sampleRate.store ((uint32_t) std::lround (sampleRate), std::memory_order_relaxed);
        sl.numChannels.store ((uint32_t) std::min (numChannels, 2), std::memory_order_relaxed);

        AudioRing ring (sl.writeIndex, sl.readIndex, sl.audio[0], sl.audio[1], ringFrames);
        ring.write (channels, std::min (numChannels, 2), numSamples); // dropped if the app isn't reading

        sl.audioBeat.fetch_add (1, std::memory_order_release);
    }
}
