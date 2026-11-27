#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace locus::ipc
{
    /** View of a single-producer / single-consumer stereo ring living in shared memory.
        Indices are monotonic 64-bit frame counters; the buffer position is index & mask. */
    class AudioRing
    {
    public:
        AudioRing (std::atomic<uint64_t>& writeIndexToUse, std::atomic<uint64_t>& readIndexToUse,
                   float* left, float* right, int capacityFrames) noexcept
            : writeIndex (writeIndexToUse), readIndex (readIndexToUse),
              capacity (capacityFrames), mask ((uint64_t) capacityFrames - 1)
        {
            channels[0] = left;
            channels[1] = right;
        }

        int getCapacity() const noexcept { return capacity; }

        /** Frames available to read (consumer side). */
        int getNumReady() const noexcept
        {
            return (int) (writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_relaxed));
        }

        /** Producer: writes numFrames, or nothing if there is not enough free space. Mono input
            is written to both channels. */
        bool write (const float* const* input, int numChannels, int numFrames) noexcept
        {
            const auto w = writeIndex.load (std::memory_order_relaxed);
            const auto r = readIndex.load (std::memory_order_acquire);

            if ((int) (w - r) + numFrames > capacity)
                return false;

            for (int c = 0; c < 2; ++c)
                copyIn (channels[c], input[std::min (c, numChannels - 1)], w, numFrames);

            writeIndex.store (w + (uint64_t) numFrames, std::memory_order_release);
            return true;
        }

        /** Consumer: copies numFrames starting at the read position without consuming them. */
        void peek (float* left, float* right, int numFrames) const noexcept
        {
            const auto r = readIndex.load (std::memory_order_relaxed);
            copyOut (left, channels[0], r, numFrames);
            copyOut (right, channels[1], r, numFrames);
        }

        /** Consumer: consumes numFrames. */
        void advance (int numFrames) noexcept
        {
            readIndex.store (readIndex.load (std::memory_order_relaxed) + (uint64_t) numFrames, std::memory_order_release);
        }

        /** Consumer: discards everything buffered. */
        void flush() noexcept
        {
            readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
        }

    private:
        void copyIn (float* dest, const float* src, uint64_t index, int n) const noexcept
        {
            const auto start = (int) (index & mask);
            const auto first = std::min (n, capacity - start);
            std::copy (src, src + first, dest + start);
            std::copy (src + first, src + n, dest);
        }

        void copyOut (float* dest, const float* src, uint64_t index, int n) const noexcept
        {
            const auto start = (int) (index & mask);
            const auto first = std::min (n, capacity - start);
            std::copy (src + start, src + start + first, dest);
            std::copy (src, src + (n - first), dest + first);
        }

        std::atomic<uint64_t>& writeIndex;
        std::atomic<uint64_t>& readIndex;
        float* channels[2] {};
        int capacity;
        uint64_t mask;
    };
}
