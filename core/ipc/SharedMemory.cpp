#include "SharedMemory.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace locus::ipc
{
    SharedMemory::~SharedMemory()
    {
        close();
    }

    bool SharedMemory::open (const juce::String& name, size_t newSize)
    {
        close();

       #if JUCE_WINDOWS
        const auto fullName = "Local\\" + name; // keep the String alive while its pointer is used
        const auto size64 = (unsigned long long) newSize;

        auto* mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                            (DWORD) (size64 >> 32), (DWORD) (size64 & 0xffffffffu),
                                            fullName.toWideCharPointer());
        if (mapping == nullptr)
            return false;

        auto* view = MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, newSize);
        if (view == nullptr)
        {
            CloseHandle (mapping);
            return false;
        }

        handle = mapping;
        data = view;
       #else
        // POSIX names start with exactly one "/"; macOS caps them at PSHMNAMLEN (31) characters
        // and fails shm_open with ENAMETOOLONG beyond that.
        const auto fullName = "/" + name.trimCharactersAtStart ("/");
        jassert (fullName.getNumBytesAsUTF8() <= 31);
        const int descriptor = shm_open (fullName.toRawUTF8(), O_CREAT | O_RDWR, 0600);
        if (descriptor < 0)
            return false;

        // macOS allows ftruncate on a shm object only once (while its size is 0), so resize only
        // a region that is still too small; one made by the other process is left as it is.
        struct stat info {};
        if (fstat (descriptor, &info) != 0 || ((size_t) info.st_size < newSize && ftruncate (descriptor, (off_t) newSize) != 0))
        {
            ::close (descriptor);
            return false;
        }

        auto* mapped = mmap (nullptr, newSize, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (mapped == MAP_FAILED)
        {
            ::close (descriptor);
            return false;
        }

        fd = descriptor;
        data = mapped;
       #endif

        size = newSize;
        return true;
    }

    void SharedMemory::close()
    {
        if (data == nullptr)
            return;

       #if JUCE_WINDOWS
        UnmapViewOfFile (data);
        CloseHandle ((HANDLE) handle);
        handle = nullptr;
       #else
        munmap (data, size);
        ::close (fd);
        fd = -1;
       #endif

        data = nullptr;
        size = 0;
    }
}
