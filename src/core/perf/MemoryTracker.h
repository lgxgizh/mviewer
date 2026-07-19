#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// Qt-free performance ledger.
//
// MemoryTracker does NOT interpose the allocator. It *samples* counters that
// already exist in the core (CacheManager memory pools, ImageFrame live count)
// plus a best-effort OS working-set read. This is deliberate YAGNI: full
// heaptrack-style interposition is a roadmap Phase-4 concern. M10 only needs a
// repeatable, deterministic memory ledger to verify docs/performance.md budgets.
//
// Header is Qt-free. The OS-RSS read lives in the .cpp, guarded per-platform,
// and is NEVER used to fail a budget check (OS RSS is noisy). Budget checks use
// the deterministic CacheManager bytes + liveImageFrames × frameBytes.

namespace mviewer::perf
{

// ImageCache::Level order (must match core/image/ImageCache.h).
enum class MemLevel : int
{
    Metadata = 0,
    Thumbnail = 1,
    Preview = 2,
    Viewer = 3,
    LevelCount = 4
};

struct MemorySnapshot
{
    size_t cacheTotalBytes = 0;             // CacheManager::memoryUsageBytes()
    size_t cacheByLevel[4] = {0, 0, 0, 0};  // per MemLevel
    uint64_t cacheHits[4] = {0, 0, 0, 0};   // per MemLevel (from levelStats)
    uint64_t cacheMisses[4] = {0, 0, 0, 0}; // per MemLevel
    size_t liveImageFrames = 0;             // tracked via ImageFrame ctor/dtor
    size_t externalBytes = 0;               // caller-accounted in-flight buffers
    size_t peakBytes = 0;                   // max (cacheTotal + external) seen
    size_t processWorkingSetKB = 0;         // best-effort OS RSS (0 if N/A)
};

class MemoryTracker
{
  public:
    static MemoryTracker &instance();

    // Sample now from CacheManager + live-frame counter + OS; update peak;
    // return the snapshot.
    MemorySnapshot sample();

    // Manual ledger for buffers the cache doesn't count (e.g. in-flight decode
    // buffers held outside the cache during a loadDirectory sweep).
    void addExternal(size_t bytes);
    void removeExternal(size_t bytes);

    // Clears peak + external. Does NOT touch CacheManager or ImageFrame counts.
    void reset();

    // Last-seen maximum of (cacheTotal + external).
    MemorySnapshot peak() const;

    // Called by ImageFrame ctor/dtor (additive, one line each in ImageFrame.cpp).
    static void notifyFrameCreated();
    static void notifyFrameDestroyed();

  private:
    MemoryTracker() = default;

    mutable std::atomic<size_t> m_external{0};
    mutable std::atomic<size_t> m_peak{0};
    mutable std::atomic<size_t> m_liveFrames{0};
    mutable std::atomic<size_t> m_peakLiveFrames{0};
    mutable std::mutex m_peakMtx;
};

} // namespace mviewer::perf
