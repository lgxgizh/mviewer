#include "core/perf/MemoryTracker.h"

#include "core/cache/CacheManager.h"

#include <QtGlobal> // defines Q_OS_WIN / Q_OS_LINUX for the OS-RSS branch below

#include <algorithm>

#ifdef Q_OS_WIN
// clang-format off
// windows.h must precede psapi.h: on the Windows SDK, psapi.h relies on types
// defined by windows.h, so the include order is load-bearing, not cosmetic.
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace mviewer::perf
{

MemoryTracker &MemoryTracker::instance()
{
    static MemoryTracker inst;
    return inst;
}

void MemoryTracker::notifyFrameCreated()
{
    auto &self = instance();
    const size_t n = self.m_liveFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    size_t expected = self.m_peakLiveFrames.load(std::memory_order_relaxed);
    while (n > expected &&
           !self.m_peakLiveFrames.compare_exchange_weak(expected, n, std::memory_order_relaxed))
    {
    }
}

void MemoryTracker::notifyFrameDestroyed()
{
    auto &self = instance();
    size_t cur = self.m_liveFrames.load(std::memory_order_relaxed);
    while (cur > 0 &&
           !self.m_liveFrames.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed))
    {
    }
}
static size_t processWorkingSetKB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.WorkingSetSize / 1024);
    return 0;
#elif defined(Q_OS_LINUX)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return static_cast<size_t>(ru.ru_maxrss); // KB on Linux
    return 0;
#else
    return 0;
#endif
}

MemorySnapshot MemoryTracker::sample()
{
    MemorySnapshot s;
    auto &cm = CacheManager::instance();

    s.cacheTotalBytes = cm.memoryUsageBytes();
    for (int i = 0; i < 4; ++i)
    {
        const CacheLevel lvl = static_cast<CacheLevel>(i);
        const CacheLevelStats st = cm.levelStats(lvl);
        s.cacheByLevel[i] = st.bytes;
        s.cacheHits[i] = st.hits;
        s.cacheMisses[i] = st.misses;
    }

    s.liveImageFrames = m_liveFrames.load(std::memory_order_relaxed);
    s.externalBytes = m_external.load(std::memory_order_relaxed);
    s.processWorkingSetKB = processWorkingSetKB();

    const size_t total = s.cacheTotalBytes + s.externalBytes;
    size_t prev = m_peak.load(std::memory_order_relaxed);
    while (total > prev && !m_peak.compare_exchange_weak(prev, total, std::memory_order_relaxed))
        ; // lock-free peak update

    s.peakBytes = m_peak.load(std::memory_order_relaxed);
    return s;
}

void MemoryTracker::addExternal(size_t bytes)
{
    m_external.fetch_add(bytes, std::memory_order_relaxed);
}

void MemoryTracker::removeExternal(size_t bytes)
{
    size_t cur = m_external.load(std::memory_order_relaxed);
    while (cur < bytes && !m_external.compare_exchange_weak(cur, 0, std::memory_order_relaxed))
        ;
    const size_t dec = (cur >= bytes) ? bytes : cur;
    m_external.fetch_sub(dec, std::memory_order_relaxed);
}

void MemoryTracker::reset()
{
    m_peak.store(0, std::memory_order_relaxed);
    m_external.store(0, std::memory_order_relaxed);
    m_peakLiveFrames.store(0, std::memory_order_relaxed);
}

MemorySnapshot MemoryTracker::peak() const
{
    MemorySnapshot s;
    s.peakBytes = m_peak.load(std::memory_order_relaxed);
    s.externalBytes = m_external.load(std::memory_order_relaxed);
    s.liveImageFrames = m_peakLiveFrames.load(std::memory_order_relaxed);
    return s;
}

} // namespace mviewer::perf
