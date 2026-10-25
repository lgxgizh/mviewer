#include "core/MemoryTrim.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MVIEWER_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define MVIEWER_ASAN 1
#endif

#if defined(_WIN32) && !defined(MVIEWER_ASAN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#elif (defined(__GLIBC__) || (defined(__linux__) && !defined(__ANDROID__))) &&                     \
    !defined(MVIEWER_ASAN)
#include <malloc.h>
#endif

namespace mviewer::core
{

void releaseFreeHeapToOs()
{
    // Under AddressSanitizer the process shadow/memory map is huge. On Windows,
    // SetProcessWorkingSetSize(-1, -1) then walks that map and can stall the UI
    // thread for minutes — long enough for ctest's 300 s m61_roi_workflow
    // TIMEOUT (Nightly clang-cl ASan). Skip the trim entirely under ASan; the
    // OS reclaims on process exit either way.
#if defined(MVIEWER_ASAN)
    return;
#elif defined(_WIN32)
    _heapmin();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#elif defined(__GLIBC__)
    malloc_trim(0);
#endif
}

} // namespace mviewer::core
