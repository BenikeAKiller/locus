#pragma once

#include <juce_core/juce_core.h>

namespace locus::ipc
{
    /** A named block of memory shared between processes on this machine.
        Windows: CreateFileMappingW in the session-local namespace. macOS: shm_open + mmap.
        New memory is zero-filled by the OS. */
    class SharedMemory
    {
    public:
        SharedMemory() = default;
        ~SharedMemory();

        /** Opens the region, creating it if needed. Returns false on failure. */
        bool open (const juce::String& name, size_t size);
        void close();

        void* getData() const noexcept { return data; }
        bool isOpen() const noexcept   { return data != nullptr; }

    private:
        void* data = nullptr;
        size_t size = 0;
        void* handle = nullptr; // Windows mapping handle
        int fd = -1;            // POSIX descriptor

        JUCE_DECLARE_NON_COPYABLE (SharedMemory)
    };
}
