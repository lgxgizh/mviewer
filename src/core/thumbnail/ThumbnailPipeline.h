#pragma once

#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"

#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─── ThumbnailPipeline ───────────────────────────────────────────────────────
// Independent thumbnail subsystem (Architect P1-③): background decode ->
// in-memory LRU -> visible queue (priority) -> predictive loading.
//
// Owned by core/ (Qt-permitted). It does NOT know about QPixmap/QWidget; it
// produces ImageData thumbnails and delivers them via an injected callback.
// The UI adapts ImageData -> QPixmap and forwards via signal. Decode is
// injected so the pipeline is unit-testable without files or a display.
//
// Work is submitted to the shared TaskScheduler (ThumbnailPool), reusing the
// priority/cancel machinery instead of spinning up a private worker thread.

struct ThumbnailPipeline
{
    using DecodeFn = std::function<ImageData(const std::string &path, int size)>;
    using ResultFn = std::function<void(const std::string &path, const ImageData &thumb)>;

    int thumbSize = 256;
    size_t memCacheMax = 512; // hot thumbnails retained in memory (LRU)

    // Inject the decode step (default: Decoder::decodeScaled). Tests inject a fake.
    void setDecodeFn(DecodeFn fn)
    {
        m_decode = std::move(fn);
    }
    // Deliver decoded thumbnails here (called on the scheduler worker thread).
    void setResultFn(ResultFn fn)
    {
        m_result = std::move(fn);
    }

    // Full directory listing (image paths, in display order).
    void setSources(const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_sources = paths;
    }

    // The currently visible item range [begin, end). Visible items are decoded
    // at Thumbnail priority (ahead of predictive neighbors). Must be called
    // whenever the viewport scrolls/resizes. Re-submits only missing items.
    void setVisibleRange(size_t begin, size_t end)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_visibleBegin = begin;
        m_visibleEnd = end;
        scheduleLocked();
    }

    // Number of neighbors beyond the visible range to pre-decode at Background
    // priority (predictive loading for fast scroll). Default 16.
    void setPredictiveCount(size_t n)
    {
        m_predictive = n;
    }

    // Synchronous cache probe: returns the cached thumbnail if present, else
    // null and kicks an async decode (respecting visible/predictive ordering).
    ImageData request(const std::string &path)
    {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_memCache.find(path);
            if (it != m_memCache.end())
            {
                m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
                return it->second.data;
            }
        }
        // Kick scheduling in case this path is newly visible.
        std::lock_guard<std::mutex> lk(m_mtx);
        scheduleLocked();
        return ImageData{};
    }

    // Cancel all outstanding thumbnail tasks (e.g. on directory switch).
    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto &kv : m_handles)
            TaskScheduler::cancel(kv.second);
        m_handles.clear();
        m_memCache.clear();
        m_lru.clear();
        m_sources.clear();
        m_pending.clear();
    }

    size_t memCacheSize() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_memCache.size();
    }

    static ThumbnailPipeline &instance()
    {
        static ThumbnailPipeline inst;
        return inst;
    }

  private:
    struct MemEntry
    {
        ImageData data;
        std::list<std::string>::iterator lruIt;
    };

    // Must hold m_mtx. Enqueues visible items (Thumbnail prio) then predictive
    // neighbors (also Thumbnail prio, enqueued AFTER visible).
    //
    // Priority note (M10 P1 fix): neighbors were previously submitted at
    // Background priority, which maps to a SEPARATE QThreadPool that runs
    // concurrently with the Thumbnail pool. Under many cores the Background
    // pool finished neighbor decodes BEFORE some visible decodes still queued
    // in the Thumbnail pool -> tier_ordering=VIOLATED (visible not strictly
    // ahead). Submitting both to the SAME Thumbnail pool with visible enqueued
    // first gives FIFO ordering: visible tasks occupy the front of the queue
    // and drain before any neighbor starts, so background never preempts
    // visible. No Scheduler redesign -- just correct pool usage.
    void scheduleLocked()
    {
        if (!m_decode)
            return;
        const size_t n = m_sources.size();
        // Visible range (clamped) -- enqueued FIRST so it leads the queue.
        const size_t vb = std::min(m_visibleBegin, n);
        const size_t ve = std::min(m_visibleEnd, n);
        for (size_t i = vb; i < ve; ++i)
            enqueueLocked(m_sources[i], TaskScheduler::Priority::Thumbnail);
        // Predictive neighbors after the visible range -- same pool, behind
        // visible in FIFO order, so they never preempt visible work.
        const size_t pe = std::min(ve + m_predictive, n);
        for (size_t i = ve; i < pe; ++i)
            enqueueLocked(m_sources[i], TaskScheduler::Priority::Thumbnail);
        // (Predictive *before* the visible range is intentionally omitted: the
        // user scrolls forward; reverse prefetch can be added later.)
    }

    void enqueueLocked(const std::string &path, TaskScheduler::Priority prio)
    {
        if (m_memCache.count(path) || m_pending.count(path))
            return;
        m_pending.insert(path);
        const int size = thumbSize;
        DecodeFn decode = m_decode;
        ResultFn result = m_result;
        auto handle = TaskScheduler::instance().submit(
            prio,
            [this, path, size, decode, result](const TaskScheduler::TaskContext &)
            {
                ImageData thumb = decode(path, size);
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    m_pending.erase(path);
                    if (!thumb.isNull())
                    {
                        cacheLocked(path, thumb);
                        // result callback may run on worker thread; caller adapts.
                        if (result)
                            result(path, thumb);
                    }
                }
            });
        if (handle)
            m_handles[path] = handle;
    }

    void cacheLocked(const std::string &path, const ImageData &data)
    {
        MemEntry e;
        e.data = data;
        m_lru.push_front(path);
        e.lruIt = m_lru.begin();
        m_memCache[path] = e;
        while (m_memCache.size() > memCacheMax)
        {
            const std::string &back = m_lru.back();
            m_memCache.erase(back);
            m_lru.pop_back();
        }
    }

    mutable std::mutex m_mtx;
    std::vector<std::string> m_sources;
    size_t m_visibleBegin = 0;
    size_t m_visibleEnd = 0;
    size_t m_predictive = 16;
    DecodeFn m_decode = [](const std::string &path, int size)
    { return Decoder::decodeScaled(path, size); };
    ResultFn m_result;
    std::unordered_map<std::string, MemEntry> m_memCache;
    std::list<std::string> m_lru;
    std::unordered_map<std::string, TaskScheduler::TaskHandle> m_handles;
    std::unordered_set<std::string> m_pending;
};
