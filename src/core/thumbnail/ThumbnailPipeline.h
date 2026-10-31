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
// Owned by core/ (Qt-free headers). It produces ImageData thumbnails and delivers
// them via callbacks. Supports multiple isolated consumers (e.g. primary pane
// cid=0, secondary pane cid=1 for Split Browse View).
struct ThumbnailPipeline
{
    using DecodeFn = std::function<ImageData(const std::string &path, int size)>;
    using ResultFn = std::function<void(const std::string &path, int size, const ImageData &thumb)>;
    using ResultToken = uint64_t;

    size_t memCacheMax = 512;
    size_t memCacheMaxBytes = size_t{96} * 1024 * 1024; // 96 MiB budget (M55)

    int currentThumbSize() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return thumbSize;
    }

    void setMemCacheMaxBytes(size_t bytes)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        memCacheMaxBytes = bytes;
        pruneCacheLocked();
    }

    void setDecodeFn(DecodeFn fn)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_decode = std::move(fn);
    }

    void setResultFn(ResultFn fn)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_result = std::move(fn);
    }

    ResultToken addResultFn(ResultFn fn)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!fn)
            return 0;
        ResultToken token = ++m_nextResultToken;
        if (token == 0)
            token = ++m_nextResultToken;
        m_subscribers.push_back(ResultSubscriber{token, std::move(fn)});
        return token;
    }

    void removeResultFn(ResultToken token)
    {
        if (token == 0)
            return;
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it)
        {
            if (it->token == token)
            {
                m_subscribers.erase(it);
                return;
            }
        }
    }

    bool hasResultListeners() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return static_cast<bool>(m_result) || !m_subscribers.empty();
    }
    void restoreDefaultDecodeFn()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_decode = [](const std::string &path, int size)
        { return Decoder::decodeScaled(path, size); };
    }
    void removeConsumer(int cid)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        cancelHandlesForConsumerLocked(cid);
        m_consumers.erase(cid);
    }

    // Default consumer (cid = 0) source setters
    void setSources(const std::vector<std::string> &paths)
    {
        setSourcesForConsumer(0, paths);
    }

    void appendSources(const std::vector<std::string> &paths)
    {
        appendSourcesForConsumer(0, paths);
    }

    void replaceSources(const std::vector<std::string> &paths)
    {
        replaceSourcesForConsumer(0, paths);
    }

    void updateSources(const std::vector<std::string> &paths)
    {
        updateSourcesForConsumer(0, paths);
    }

    void setVisibleRange(size_t begin, size_t end)
    {
        setVisibleRangeForConsumer(0, begin, end);
    }

    // Consumer-isolated methods (cid = 0 for primary, cid = 1 for secondary)
    void setSourcesForConsumer(int cid, const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConsumerSlot &slot = m_consumers[cid];
        ++slot.gen;
        if (cid == 0)
            ++m_gen;
        cancelHandlesForConsumerLocked(cid);
        slot.sources = paths;
        if (cid == 0)
            m_sources = paths;
        slot.visibleBegin = 0;
        slot.visibleEnd = 0;
        if (m_consumers.size() <= 1)
        {
            m_pending.clear();
            m_failed.clear();
            m_attemptedKeys.clear();
        }
        else
        {
            for (const auto &p : paths)
            {
                const std::string prefix = p + "\x1f";
                for (auto it = m_failed.begin(); it != m_failed.end();)
                {
                    if (it->rfind(prefix, 0) == 0)
                        it = m_failed.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_attemptedKeys.begin(); it != m_attemptedKeys.end();)
                {
                    if (it->rfind(prefix, 0) == 0)
                        it = m_attemptedKeys.erase(it);
                    else
                        ++it;
                }
            }
        }
        cancelObsoleteHandlesLocked();
    }

    void appendSourcesForConsumer(int cid, const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConsumerSlot &slot = m_consumers[cid];
        slot.sources.insert(slot.sources.end(), paths.begin(), paths.end());
        if (cid == 0)
            m_sources = slot.sources;
    }

    // Failed keys block scroll refills. A source rebuild (filter, sort, scan)
    // drops them for paths that are still listed so the next schedule tries
    // once. scheduleLocked does not clear m_failed.
    void dropFailedForPathsLocked(const std::vector<std::string> &paths)
    {
        if (m_failed.empty())
            return;
        for (const auto &p : paths)
        {
            const std::string prefix = p + "\x1f";
            for (auto it = m_failed.begin(); it != m_failed.end();)
            {
                if (it->rfind(prefix, 0) == 0)
                    it = m_failed.erase(it);
                else
                    ++it;
            }
        }
    }

    void replaceSourcesForConsumer(int cid, const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConsumerSlot &slot = m_consumers[cid];
        slot.sources = paths;
        if (cid == 0)
            m_sources = paths;
        dropFailedForPathsLocked(paths);
        cancelObsoleteHandlesLocked();
        scheduleLocked(false);
    }

    void updateSourcesForConsumer(int cid, const std::vector<std::string> &paths)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConsumerSlot &slot = m_consumers[cid];
        slot.sources = paths;
        if (cid == 0)
            m_sources = paths;
        dropFailedForPathsLocked(paths);
        cancelObsoleteHandlesLocked();
        scheduleLocked(false);
    }

    void setVisibleRangeForConsumer(int cid, size_t begin, size_t end)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConsumerSlot &slot = m_consumers[cid];
        slot.visibleBegin = begin;
        slot.visibleEnd = end;
        if (cid == 0)
        {
            m_visibleBegin = begin;
            m_visibleEnd = end;
        }
        cancelObsoleteHandlesLocked();
        scheduleLocked(false);
    }

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
            m_memCacheBytes -= it->second.data.byteSize();
            m_lru.erase(it->second.lruIt);
            it = m_memCache.erase(it);
        }
        for (auto it = m_attemptedKeys.begin(); it != m_attemptedKeys.end();)
        {
            if (it->rfind(prefix, 0) != 0)
            {
                ++it;
                continue;
            }
            it = m_attemptedKeys.erase(it);
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
        for (auto it = m_failed.begin(); it != m_failed.end();)
        {
            if (it->rfind(prefix, 0) != 0)
            {
                ++it;
                continue;
            }
            it = m_failed.erase(it);
        }
    }

    void setPredictiveCount(size_t n)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_predictive = n;
    }

    void setPredictiveCountForConsumer(int cid, size_t n)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_consumers.find(cid);
        if (it != m_consumers.end())
            it->second.predictive = n;
        else
            m_predictive = n;
    }

    static size_t recommendedPredictiveCount(size_t directorySize, bool highLatencyIo)
    {
        if (highLatencyIo)
            return directorySize > 500 ? 12 : 8;
        if (directorySize > 2000)
            return 96;
        if (directorySize > 500)
            return 64;
        return 48;
    }

    void setThumbSize(int size)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (size == thumbSize)
            return;
        thumbSize = size;
        ++m_gen;
        for (auto &kv : m_consumers)
            ++kv.second.gen;
        cancelHandlesLocked();
        m_memCache.clear();
        m_lru.clear();
        m_pending.clear();
        m_failed.clear();
        m_attemptedKeys.clear();
        m_memCacheBytes = 0;
    }

    ImageData findCached(const std::string &path, int size)
    {
        if (path.empty() || size <= 0)
            return ImageData{};
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_memCache.find(key(path, size));
        if (it != m_memCache.end())
        {
            ++m_hits;
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
            return it->second.data;
        }
        return ImageData{};
    }

    ImageData request(const std::string &path, int size)
    {
        if (path.empty() || size <= 0)
            return ImageData{};
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_memCache.find(key(path, size));
        if (it != m_memCache.end())
        {
            ++m_hits;
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
            return it->second.data;
        }
        ++m_misses;
        scheduleLocked(false);
        return ImageData{};
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ++m_gen;
        for (auto &kv : m_consumers)
            ++kv.second.gen;
        cancelHandlesLocked();
        m_memCache.clear();
        m_lru.clear();
        m_sources.clear();
        m_consumers.clear();
        m_pending.clear();
        m_failed.clear();
        m_attemptedKeys.clear();
        m_pathRevisions.clear();
        m_memCacheBytes = 0;
    }

    size_t memCacheSize() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_memCache.size();
    }

    size_t memCacheBytes() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_memCacheBytes;
    }

    uint64_t hits() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_hits;
    }

    uint64_t misses() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_misses;
    }

    double hitRatio() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const uint64_t total = m_hits + m_misses;
        return total == 0 ? 0.0 : static_cast<double>(m_hits) / static_cast<double>(total);
    }

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

    uint64_t generation() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_gen;
    }

    uint64_t consumerGeneration(int cid) const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_consumers.find(cid);
        return it != m_consumers.end() ? it->second.gen : m_gen;
    }

    static ThumbnailPipeline &instance()
    {
        static ThumbnailPipeline inst;
        return inst;
    }

  private:
    int thumbSize = 256;

    struct ConsumerSlot
    {
        std::vector<std::string> sources;
        size_t visibleBegin = 0;
        size_t visibleEnd = 0;
        uint64_t gen = 0;
        size_t predictive = 0;
    };

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

    static std::string handleKey(int cid, const std::string &k, uint64_t gen, uint64_t owner)
    {
        return std::to_string(cid) + "\x1e" + k + "\x1e" + std::to_string(gen) + "\x1f" +
               std::to_string(owner);
    }

    void pruneCacheLocked()
    {
        while (!m_lru.empty() && (m_memCache.size() > memCacheMax ||
                                  (memCacheMaxBytes > 0 && m_memCacheBytes > memCacheMaxBytes)))
        {
            const std::string &back = m_lru.back();
            auto it = m_memCache.find(back);
            if (it != m_memCache.end())
            {
                m_memCacheBytes -= it->second.data.byteSize();
                m_memCache.erase(it);
            }
            m_lru.pop_back();
        }
    }

    void cacheLocked(const std::string &k, const std::string &path, const ImageData &data)
    {
        (void)path;
        if (k.empty() || data.isNull())
            return;
        const size_t bytes = data.byteSize();
        if (memCacheMaxBytes > 0 && bytes > memCacheMaxBytes)
            return;

        auto existing = m_memCache.find(k);
        if (existing != m_memCache.end())
        {
            m_memCacheBytes -= existing->second.data.byteSize();
            m_lru.erase(existing->second.lruIt);
            m_memCache.erase(existing);
        }
        MemEntry e;
        e.data = data;
        m_lru.push_front(k);
        e.lruIt = m_lru.begin();
        m_memCacheBytes += bytes;
        m_memCache[k] = e;
        pruneCacheLocked();
    }

    bool visibleDemandSatisfiedLocked(const std::vector<std::string> &sources, size_t vb,
                                      size_t ve) const
    {
        for (size_t i = vb; i < ve; ++i)
        {
            const std::string k = key(sources[i], thumbSize);
            if (m_memCache.count(k) == 0 && m_failed.count(k) == 0)
                return false;
        }
        return true;
    }

    void scheduleLocked(bool refillOnly)
    {
        if (!m_decode)
            return;

        auto scheduleForConsumer = [this, refillOnly](int cid, const ConsumerSlot &slot)
        {
            const size_t n = slot.sources.size();
            const size_t vb = std::min(slot.visibleBegin, n);
            const size_t ve = std::min(slot.visibleEnd, n);
            if (!visibleDemandSatisfiedLocked(slot.sources, vb, ve))
            {
                size_t inFlight = 0;
                for (size_t i = vb; i < ve; ++i)
                {
                    if (m_pending.count(key(slot.sources[i], thumbSize)) != 0)
                        ++inFlight;
                }
                for (size_t i = vb; i < ve && inFlight < kVisibleFirstWave; ++i)
                {
                    const std::string k = key(slot.sources[i], thumbSize);
                    if (m_memCache.count(k) != 0 || m_failed.count(k) != 0 ||
                        m_pending.count(k) != 0)
                        continue;
                    enqueueLocked(slot.sources[i], cid, slot.gen,
                                  TaskScheduler::Priority::Thumbnail, refillOnly);
                    if (m_pending.count(k) != 0)
                        ++inFlight;
                }
                return;
            }
            for (size_t i = vb; i < ve; ++i)
                enqueueLocked(slot.sources[i], cid, slot.gen, TaskScheduler::Priority::Thumbnail,
                              refillOnly);
            const size_t pred = slot.predictive > 0 ? slot.predictive : m_predictive;
            const size_t pe = std::min(ve + pred, n);
            for (size_t i = ve; i < pe; ++i)
                enqueueLocked(slot.sources[i], cid, slot.gen, TaskScheduler::Priority::Thumbnail,
                              refillOnly);
        };

        if (m_consumers.empty())
        {
            ConsumerSlot defSlot{m_sources, m_visibleBegin, m_visibleEnd, m_gen};
            scheduleForConsumer(0, defSlot);
        }
        else
        {
            for (const auto &kv : m_consumers)
                scheduleForConsumer(kv.first, kv.second);
        }
    }

    void enqueueLocked(const std::string &path, int cid, uint64_t slotGen,
                       TaskScheduler::Priority prio, bool refillOnly)
    {
        const int size = thumbSize;
        const std::string k = key(path, size);
        if (m_memCache.count(k) || m_failed.count(k))
            return;
        auto pit = m_pending.find(k);
        if (pit != m_pending.end())
            return;
        if (refillOnly && m_attemptedKeys.count(k) != 0)
            return;
        const uint64_t owner = ++m_nextOwner;
        const std::string ownerKey = handleKey(cid, k, slotGen, owner);
        m_pending[k] = PendingEntry{owner, ownerKey};
        DecodeFn decode = m_decode;
        ResultFn result = m_result;
        const uint64_t pathRevision = m_pathRevisions[path];
        m_attemptedKeys.insert(k);

        auto handle = TaskScheduler::instance().submit(
            prio,
            [this, path, size, k, cid, slotGen, owner, ownerKey, decode, result,
             pathRevision](const TaskScheduler::TaskContext &ctx)
            {
                auto abandonPending = [this, k, owner, ownerKey]()
                {
                    m_handles.erase(ownerKey);
                    auto pit = m_pending.find(k);
                    if (pit != m_pending.end() && pit->second.owner == owner)
                        m_pending.erase(pit);
                };
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    auto it = m_consumers.find(cid);
                    const bool stale = ctx.isCancelled() || m_pathRevisions[path] != pathRevision ||
                                       (it != m_consumers.end() && it->second.gen != slotGen) ||
                                       (it == m_consumers.end() && cid == 0 && m_gen != slotGen);
                    if (stale)
                    {
                        abandonPending();
                        return;
                    }
                }
                ImageData thumb;
                if (decode)
                {
                    try
                    {
                        thumb = decode(path, size);
                    }
                    catch (...)
                    {
                        thumb = ImageData{};
                    }
                }
                ResultFn cb;
                std::vector<ResultFn> subscribers;
                ImageData deliver;
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    m_handles.erase(ownerKey);
                    auto pit = m_pending.find(k);
                    if (pit == m_pending.end() || pit->second.owner != owner)
                        return;
                    m_pending.erase(pit);
                    auto it = m_consumers.find(cid);
                    if (it != m_consumers.end() && it->second.gen != slotGen)
                        return;
                    if (it == m_consumers.end() && cid == 0 && m_gen != slotGen)
                        return;
                    if (m_pathRevisions[path] != pathRevision)
                        return;
                    if (!thumb.isNull())
                    {
                        cacheLocked(k, path, thumb);
                        deliver = thumb;
                    }
                    else
                    {
                        m_failed.insert(k);
                        deliver = ImageData{};
                    }
                    cb = result;
                    subscribers.reserve(m_subscribers.size());
                    for (const auto &sub : m_subscribers)
                    {
                        if (sub.fn)
                            subscribers.push_back(sub.fn);
                    }
                    scheduleLocked(true);
                }
                auto deliverOne = [&](const ResultFn &fn)
                {
                    if (!fn)
                        return;
                    try
                    {
                        fn(path, size, deliver);
                    }
                    catch (...)
                    {
                    }
                };
                deliverOne(cb);
                for (const ResultFn &sub : subscribers)
                    deliverOne(sub);
            });

        if (handle)
        {
            m_handles[ownerKey] = handle;
        }
        else
        {
            auto pendIt = m_pending.find(k);
            if (pendIt != m_pending.end() && pendIt->second.owner == owner)
                m_pending.erase(pendIt);
            m_attemptedKeys.erase(k);
        }
    }

    void cancelObsoleteHandlesLocked()
    {
        if (m_pending.empty())
            return;
        std::unordered_set<std::string> keep;
        auto addKeepFor = [this, &keep](const ConsumerSlot &slot)
        {
            const size_t n = slot.sources.size();
            const size_t vb = std::min(slot.visibleBegin, n);
            const size_t ve = std::min(slot.visibleEnd, n);
            for (size_t i = vb; i < ve; ++i)
                keep.insert(key(slot.sources[i], thumbSize));
            const size_t pred = slot.predictive > 0 ? slot.predictive : m_predictive;
            const size_t pe = std::min(ve + pred, n);
            for (size_t i = ve; i < pe; ++i)
                keep.insert(key(slot.sources[i], thumbSize));
        };
        if (m_consumers.empty())
        {
            ConsumerSlot defSlot{m_sources, m_visibleBegin, m_visibleEnd, m_gen};
            addKeepFor(defSlot);
        }
        else
        {
            for (const auto &kv : m_consumers)
                addKeepFor(kv.second);
        }

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

    void cancelHandlesForConsumerLocked(int cid)
    {
        const std::string prefix = std::to_string(cid) + "\x1e";
        for (auto it = m_handles.begin(); it != m_handles.end();)
        {
            if (it->first.rfind(prefix, 0) == 0)
            {
                TaskScheduler::cancel(it->second);
                it = m_handles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void cancelHandlesLocked()
    {
        for (auto &kv : m_handles)
            TaskScheduler::cancel(kv.second);
        m_handles.clear();
    }

    mutable std::mutex m_mtx;
    std::vector<std::string> m_sources;
    size_t m_visibleBegin = 0;
    size_t m_visibleEnd = 0;
    size_t m_predictive = 16;
    static constexpr size_t kVisibleFirstWave = 16;
    uint64_t m_gen = 0;
    std::unordered_map<int, ConsumerSlot> m_consumers;

    DecodeFn m_decode = [](const std::string &path, int size)
    { return Decoder::decodeScaled(path, size); };

    struct ResultSubscriber
    {
        ResultToken token = 0;
        ResultFn fn;
    };
    ResultFn m_result;
    std::vector<ResultSubscriber> m_subscribers;
    ResultToken m_nextResultToken = 0;
    std::unordered_map<std::string, MemEntry> m_memCache;
    std::list<std::string> m_lru;
    size_t m_memCacheBytes = 0;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    std::unordered_map<std::string, TaskScheduler::TaskHandle> m_handles;
    std::unordered_map<std::string, PendingEntry> m_pending;
    std::unordered_set<std::string> m_failed;
    std::unordered_set<std::string> m_attemptedKeys;
    std::unordered_map<std::string, uint64_t> m_pathRevisions;
    uint64_t m_nextOwner = 0;
};
