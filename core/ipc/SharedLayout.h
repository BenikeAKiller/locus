#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "LocusCore.h"
#include "params/Parameters.h"

namespace locus::ipc
{
    constexpr const char* defaultSharedMemoryName = "LocusShm_v2";
    constexpr uint32_t layoutMagic   = 0x4c4f4355; // "LOCU"
    constexpr uint32_t layoutVersion = 2;
    constexpr int ringFrames = 32768; // per channel, power of two
    constexpr int numSyncParams = SourceParam::count;
    constexpr int nameWords = 8;      // 64 bytes of UTF-8

    static_assert (numSyncParams <= 32, "gesture flags are a 32-bit mask");
    static_assert (std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free
                   && std::atomic<float>::is_always_lock_free, "shared-memory atomics must be address-free");

    //==============================================================================
    /** Seqlock: one writer, any number of readers, nobody blocks. The sequence number is odd
        while a write is in progress; a reader retries if it changed during the read. */
    struct SeqLock
    {
        std::atomic<uint32_t> sequence;

        uint32_t beginWrite() noexcept
        {
            const auto s = sequence.load (std::memory_order_relaxed) + 1; // odd: writing
            sequence.store (s, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
            return s;
        }

        void endWrite (uint32_t s) noexcept { sequence.store (s + 1, std::memory_order_release); }

        /** Runs readFn until it sees a consistent snapshot. Gives up (returns false) after a
            bounded number of attempts, so a writer that died mid-write can't hang the reader. */
        template <typename ReadFn>
        bool read (ReadFn&& readFn, uint32_t* sequenceSeen = nullptr) const noexcept
        {
            for (int attempt = 0; attempt < 10000; ++attempt)
            {
                const auto before = sequence.load (std::memory_order_acquire);
                if ((before & 1u) != 0)
                    continue;

                readFn();
                std::atomic_thread_fence (std::memory_order_acquire);

                if (sequence.load (std::memory_order_relaxed) == before)
                {
                    if (sequenceSeen != nullptr)
                        *sequenceSeen = before;
                    return true;
                }
            }

            return false;
        }

        uint32_t current() const noexcept { return sequence.load (std::memory_order_acquire); }
    };

    /** Parameter values published by a Send plugin (spec Â§8, plugin -> app). */
    struct PluginParams
    {
        SeqLock lock;
        std::atomic<float> values[numSyncParams];
        std::atomic<uint32_t> appliedAppSequence; // the app -> plugin block the plugin had applied
        std::atomic<uint64_t> publisher;          // owner id of the plugin that wrote this block
    };

    /** Parameter edits made in the app (app -> plugin). Each parameter has its own change
        counter, so the plugin applies only what the app actually changed. */
    struct AppEdits
    {
        SeqLock lock;
        std::atomic<float> values[numSyncParams];
        std::atomic<uint32_t> changeCount[numSyncParams];
        std::atomic<uint32_t> gestureMask; // bit p: the app is dragging parameter p
    };

    struct SourceName
    {
        SeqLock lock;
        std::atomic<uint64_t> words[nameWords];
    };

    struct Slot
    {
        std::atomic<uint64_t> owner;      // random instance id, 0 = free
        std::atomic<uint32_t> aliveBeat;  // bumped by the plugin's timer: the owner exists
        std::atomic<uint32_t> audioBeat;  // bumped by processBlock: audio is flowing
        std::atomic<uint32_t> sampleRate; // Hz
        std::atomic<uint32_t> numChannels;
        std::atomic<uint32_t> nonRealtime; // 1 while the host renders offline

        SourceName name;
        PluginParams fromPlugin;
        AppEdits fromApp;

        // Single-producer (plugin audio thread), single-consumer (app audio thread) ring.
        std::atomic<uint64_t> writeIndex;
        std::atomic<uint64_t> readIndex;
        float audio[2][ringFrames];
    };

    struct Header
    {
        std::atomic<uint32_t> magic;
        std::atomic<uint32_t> version;
        std::atomic<uint32_t> appPid;
        std::atomic<uint32_t> appHeartbeat; // bumped by the app's timer while it runs
    };

    struct SharedLayout
    {
        Header header;
        Slot slots[maxSources];
    };

    /** Zero-filled memory is a valid empty layout; this only stamps the version. Returns false
        if the memory holds a different, incompatible layout version. */
    inline bool initialiseLayout (SharedLayout& layout) noexcept
    {
        uint32_t expected = 0;
        if (layout.header.magic.compare_exchange_strong (expected, layoutMagic))
            layout.header.version.store (layoutVersion);
        else if (expected != layoutMagic)
            return false;

        // The stamping process may not have written the version yet; zero means "about to".
        const auto version = layout.header.version.load();
        return version == layoutVersion || version == 0;
    }

    //==============================================================================
    /** UTF-8 name packed into atomic words. */
    inline void writeName (SourceName& n, const juce::String& name) noexcept
    {
        char bytes[nameWords * 8] {};
        name.copyToUTF8 (bytes, sizeof (bytes)); // truncates, always null-terminated

        const auto s = n.lock.beginWrite();
        for (int w = 0; w < nameWords; ++w)
        {
            uint64_t word;
            std::memcpy (&word, bytes + w * 8, 8);
            n.words[w].store (word, std::memory_order_relaxed);
        }
        n.lock.endWrite (s);
    }

    inline juce::String readName (const SourceName& n)
    {
        char bytes[nameWords * 8 + 1] {};
        const auto ok = n.lock.read ([&]
        {
            for (int w = 0; w < nameWords; ++w)
            {
                const auto word = n.words[w].load (std::memory_order_relaxed);
                std::memcpy (bytes + w * 8, &word, 8);
            }
        });

        return ok ? juce::String::fromUTF8 (bytes) : juce::String();
    }
}
