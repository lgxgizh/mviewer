#include "core/MemoryTrim.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#elif defined(__GLIBC__) || (defined(__linux__) && !defined(__ANDROID__))
#include <malloc.h>
#endif

namespace mviewer::core
{

void releaseFreeHeapToOs()
{
#if defined(_WIN32)
    _heapmin();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#elif defined(__GLIBC__)
    malloc_trim(0);
#endif
}

} // namespace mviewer::core
