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
//
// M25 identity & lifecycle contract:
//   * the in-memory cache is keyed by (path, requested size) — a 64px result
//     can never satisfy a 240px request;
//   * every scheduled task carries the generation it was born in; setSources()
//     and clear() bump the generation, and any result from a superseded
//     generation is dropped (never cached, never delivered) — so rapid
//     directory switches cannot pollute the current folder;
//   * cancelled tasks check the scheduler's cancel flag BEFORE decoding, so
//     stale queued work stops instead of merely being discarded afterwards.

struct ThumbnailPipeline
{
    using DecodeFn = std::function<ImageData(const std::string &path, int size)>;
    using ResultFn =
        std::function<void(const std::string &path, int size, const ImageData &thumb)>;

    int thumbSize = 256;
    size_t memCacheMax = 512; // hot thumbnails retained in memory (LRU)

    // Inject the decode step (default: Decoder::decodeScaled). Tests inject a fake.
    void setDecodeFn(DecodeFn fn)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_decode = std::move(fn);
    }
    // Deliver decoded thumbnails here (called on the scheduler worker thread).
    void setResultFn(ResultFn fn)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_result = std::move(fn);
    }

    // Full directory listing (image paths, in display order). Starts a new
    // generation: in-flight work from the previous listing is CANCELLED (queued
    // tasks stop before decoding) and the pending bookkeeping is reset so no
    // stale key can block the new listing. The consumer re-schedules via
    // setVisibleRange() once the model is built.
    void setSources(const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ++m_gen;
        cancelHandlesLocked();
        m_sources = paths;
        m_pending.clear();
        m_pathRevisions.clear();
    }

    // M54: publish discovered batches without superseding already decoded
    // thumbnails. The scanner may therefore feed the first screen while the
    // rest of a large directory is still being enumerated.
    void appendSources(const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_sources.insert(m_sources.end(), paths.begin(), paths.end());
    }

    // M54: one controlled convergence update after a progressive scan. Keep
    // the generation stable so path-keyed results remain reusable, while
    // dropping work whose old row demand is no longer current.
    void replaceSources(const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        cancelHandlesLocked();
        m_sources = paths;
        m_pending.clear();
    }

    // M56: update the live source order without bumping generation or
    // cancelling thumbnails for paths that remain in the working set.
    void updateSources(const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_sources = paths;
        cancelObsoleteHandlesLocked();
        scheduleLocked();
    }

    // M56: invalidate one source without resetting the directory generation or
    // the other visible thumbnails. In-flight work for this path is cancelled
    // and carries a per-path revision guard so a running decoder cannot publish
    // pixels from before an overwrite.
    void invalidatePath(const std::string &path)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ++m_pathRevisions[path];
        const std::string prefix = path + "\x1f";
        for (auto it = m_memCache.begin(); it != m_memCache.end();)
        {
            if (it->first.rfind(prefix, 0) != 0)
            {
                ++it;
                continue;
            }
            m_lru.erase(it->second.lruIt);
            it = m_memCache.erase(it);
        }
        for (auto it = m_pending.begin(); it != m_pending.end();)
        {
            if (it->first.rfind(prefix, 0) != 0)
            {
                ++it;
                continue;
            }
            auto handle = m_handles.find(it->second.ownerKey);
            if (handle != m_handles.end())
            {
                TaskScheduler::cancel(handle->second);
                m_handles.erase(handle);
            }
            it = m_pending.erase(it);
        }
    }

    // The currently visible item range [begin, end). Visible items are decoded
    // at Thumbnail priority (ahead of predictive neighbors). Must be called
    // whenever the viewport scrolls/resizes. Re-submits only missing items.
    void setVisibleRange(size_t begin, size_t end)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_visibleBegin = begin;
        m_visibleEnd = end;
        // M55 demand ownership: retain requests that are still inside the new
        // visible+predictive window. Only obsolete demand is cancelled. This
        // avoids cancel/re-submit churn during small scrolls and, together
        // with the per-enqueue owner token below, closes the same-generation
        // ABA window when a running decoder outlives cancellation.
        cancelObsoleteHandlesLocked();
        scheduleLocked();
    }

    // Number of neighbors beyond the visible range to pre-decode at Background
    // priority (predictive loading for fast scroll). Default 16.
    void setPredictiveCount(size_t n)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_predictive = n;
    }

    // M46: thumbnail-size change is a REAL supersession boundary. In-flight
    // decodes at the old size are cancelled (their handles are dropped and the
    // generation is bumped, so a late old-size result is neither cached nor
    // delivered), the old-size memory cache and pending keys are dropped, and
    // the caller re-schedules the visible window at the new size. Previously a
    // size change only re-queued the new size while old-size decodes kept
    // running and repopulated the cache — stale workload stayed unbounded
    // during a drag of the size slider.
    void setThumbSize(int size)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (size == thumbSize)
            return;
        thumbSize = size;
        ++m_gen;
        cancelHandlesLocked();
        m_memCache.clear();
        m_lru.clear();
        m_pending.clear();
    }

    // Synchronous cache probe: returns the cached thumbnail at `size` if
    // present, else null and kicks an async decode (respecting
    // visible/predictive ordering).
    ImageData request(const std::string &path, int size)
    {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_memCache.find(key(path, size));
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

    // Cancel all outstanding thumbnail tasks (e.g. on directory switch) and
    // start a new generation so late results are dropped.
    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ++m_gen;
        cancelHandlesLocked();
        m_memCache.clear();
        m_lru.clear();
        m_sources.clear();
        m_pending.clear();
        m_pathRevisions.clear();
    }

    size_t memCacheSize() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_memCache.size();
    }

    // M26: test observability — number of keys with an outstanding scheduler
    // handle (must return to ~0 after completion) and keys awaiting/decoding
    // (must return to 0 when the pipeline is idle).
    size_t handlesCount() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_handles.size();
    }
    size_t pendingCount() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_pending.size();
    }

    // The generation counter — bumped by setSources()/clear(). Consumers may
    // observe it to validate their own deferred work.
    uint64_t generation() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_gen;
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

    struct PendingEntry
    {
        uint64_t owner = 0;
        std::string ownerKey;
    };

    static std::string key(const std::string &path, int size)
    {
        return path + "\x1f" + std::to_string(size);
    }
    // Handle-map key includes a unique per-enqueue owner. Generation alone is
    // insufficient because a same-generation viewport update may cancel a
    // running request and immediately submit the same key again.
    static std::string handleKey(const std::string &k, uint64_t gen, uint64_t owner)
    {
        return k + "\x1e" + std::to_string(gen) + "\x1f" + std::to_string(owner);
    }

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
        const int size = thumbSize;
        const std::string k = key(path, size);
        if (m_memCache.count(k))
            return;
        auto pit = m_pending.find(k);
        if (pit != m_pending.end())
            return; // already owned by the current demand window
        const uint64_t owner = ++m_nextOwner;
        const std::string ownerKey = handleKey(k, m_gen, owner);
        m_pending[k] = PendingEntry{owner, ownerKey};
        DecodeFn decode = m_decode;
        ResultFn result = m_result;
        const uint64_t gen = m_gen;
        const uint64_t pathRevision = m_pathRevisions[path];
        auto handle = TaskScheduler::instance().submit(
            prio,
            [this, path, size, k, gen, owner, ownerKey, decode,
             result, pathRevision](const TaskScheduler::TaskContext &ctx)
            {
                // Stop stale queued work BEFORE decoding. The generation that
                // owned this key already cleared/repurposed the bookkeeping in
                // setSources()/clear(); touching m_pending here could erase the
                // NEW generation's ownership of the same key.
                if (ctx.isCancelled())
                    return;
                // M27: a throwing decoder must not leak the pipeline
                // bookkeeping (m_pending / m_handles). It is treated as a
                // decode failure — the null-thumb branch below cleans up under
                // the lock and surfaces the failure to the consumer.
                ImageData thumb;
                try
                {
                    thumb = decode(path, size);
                }
                catch (...)
                {
                    thumb = ImageData{};
                }
                ImageData deliver;
                ResultFn cb;
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    // Remove this task's own bookkeeping only — the key may
                    // belong to a newer generation by now.
                    auto pendIt = m_pending.find(k);
                    if (pendIt != m_pending.end() && pendIt->second.owner == owner)
                        m_pending.erase(pendIt);
                    m_handles.erase(ownerKey);
                    // Superseded generation: drop everything (no cache, no
                    // delivery) so old directories can never pollute the
                    // current one.
                    if (gen != m_gen || ctx.isCancelled() ||
                        m_pathRevisions[path] != pathRevision)
                        return;
                    if (!thumb.isNull())
                    {
                        cacheLocked(k, path, thumb);
                        // Result callback is delivered OUTSIDE the lock (M27):
                        // a callback that re-enters request()/setSources()/
                        // clear() must never deadlock against the pipeline
                        // mutex. The state mutations above are done; only the
                        // user-visible delivery remains.
                        deliver = thumb;
                        cb = result;
                    }
                    else
                    {
                        // M24: surface decode FAILURES to the consumer (null
                        // thumb) so the UI can mark the cell as failed instead
                        // of showing an eternal loading state. The consumer
                        // decides whether/how to cache the failure.
                        deliver = ImageData{};
                        cb = result;
                    }
                }
                if (cb)
                {
                    // M27: the result callback is user code running on the
                    // worker — a throw must never escape the pool (an uncaught
                    // exception in a worker thread terminates the process).
                    // Bookkeeping was already cleaned under the lock before
                    // delivery, so a throw here leaves zero residue.
                    try
                    {
                        cb(path, size, deliver);
                    }
                    catch (...)
                    {
                    }
                }
            });
        if (handle)
        {
            m_handles[ownerKey] = handle;
        }
        else
        {
            // Scheduler rejected the submission (paused / saturated): the key
            // must remain schedulable — a later setVisibleRange/request will
            // retry it.
            auto pendIt = m_pending.find(k);
            if (pendIt != m_pending.end() && pendIt->second.owner == owner)
                m_pending.erase(pendIt);
        }
    }

    // Must hold m_mtx. A same-generation viewport update retains the exact
    // intersection of old and new demand. Each obsolete request is removed
    // by its own owner key; a later same-key request therefore cannot be
    // erased by the old task's completion.
    void cancelObsoleteHandlesLocked()
    {
        std::unordered_set<std::string> keep;
        const size_t n = m_sources.size();
        const size_t vb = std::min(m_visibleBegin, n);
        const size_t ve = std::min(m_visibleEnd, n);
        keep.reserve((ve - vb) + m_predictive);
        for (size_t i = vb; i < ve; ++i)
            keep.insert(key(m_sources[i], thumbSize));
        const size_t pe = std::min(ve + m_predictive, n);
        for (size_t i = ve; i < pe; ++i)
            keep.insert(key(m_sources[i], thumbSize));

        for (auto it = m_pending.begin(); it != m_pending.end();)
        {
            if (keep.find(it->first) != keep.end())
            {
                ++it;
                continue;
            }
            auto hit = m_handles.find(it->second.ownerKey);
            if (hit != m_handles.end())
            {
                TaskScheduler::cancel(hit->second);
                m_handles.erase(hit);
            }
            it = m_pending.erase(it);
        }
    }

    // Must hold m_mtx. Cancels every outstanding pipeline task (sets its
    // scheduler cancel token) and drops the pipeline bookkeeping for them.
    // Queued tasks still run on the pool but exit before decoding; in-flight
    // decodes finish and are dropped by the generation guard.
    void cancelHandlesLocked()
    {
        for (auto &kv : m_handles)
            TaskScheduler::cancel(kv.second);
        m_handles.clear();
    }

    void cacheLocked(const std::string &k, const std::string &path, const ImageData &data)
    {
        // A cancelled same-generation decode may still finish after a newer
        // request for the same key has been queued. Replace the existing
        // entry instead of adding a second LRU node for the same key; otherwise
        // a later path invalidation would erase a stale iterator and corrupt
        // the cache.
        auto existing = m_memCache.find(k);
        if (existing != m_memCache.end())
        {
            m_lru.erase(existing->second.lruIt);
            m_memCache.erase(existing);
        }
        MemEntry e;
        e.data = data;
        m_lru.push_front(k);
        e.lruIt = m_lru.begin();
        m_memCache[k] = e;
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
    uint64_t m_gen = 0;
    DecodeFn m_decode = [](const std::string &path, int size)
    { return Decoder::decodeScaled(path, size); };
    ResultFn m_result;
    std::unordered_map<std::string, MemEntry> m_memCache;
    std::list<std::string> m_lru;
    // Outstanding scheduler handles, keyed by (path, size, generation, owner). Tasks
    // remove their own entry on completion; setSources()/clear() cancel + drop
    // them wholesale — the map never accumulates the browse history.
    std::unordered_map<std::string, TaskScheduler::TaskHandle> m_handles;
    // Pending keys: (path, size) -> unique request owner. An obsolete request
    // can never block or erase a later same-key request in the same generation.
    std::unordered_map<std::string, PendingEntry> m_pending;
    std::unordered_map<std::string, uint64_t> m_pathRevisions;
    uint64_t m_nextOwner = 0;
};
