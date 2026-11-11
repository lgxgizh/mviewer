#include "core/memory/MemoryBudget.h"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mviewer::core
{
namespace
{
constexpr size_t kGiB = size_t{1024} * 1024 * 1024;
constexpr size_t kFloorBytes = size_t{16} * 1024 * 1024;
} // namespace

size_t physicalMemoryBytes()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return 0;
    return static_cast<size_t>(status.ullTotalPhys);
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0)
        return 0;
    return static_cast<size_t>(pages) * static_cast<size_t>(pageSize);
#endif
}

double inMemoryCacheScale(size_t physicalBytes)
{
    if (physicalBytes == 0 || physicalBytes >= 12 * kGiB)
        return 1.0;
    if (physicalBytes >= 8 * kGiB)
        return 0.75;
    return 0.5;
}

size_t scaledCacheBytes(size_t defaultBytes, double scale)
{
    if (defaultBytes < kFloorBytes)
        return defaultBytes;
    const auto scaled = static_cast<size_t>(static_cast<double>(defaultBytes) * scale);
    return std::max(scaled, kFloorBytes);
}

} // namespace mviewer::core
