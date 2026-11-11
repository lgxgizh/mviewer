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

    ResultToken addResultFn(ResultFn fn);
    void removeResultFn(ResultToken token);

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
    void setSourcesForConsumer(int cid, const std::vector<std::string> &paths);
    void appendSourcesForConsumer(int cid, const std::vector<std::string> &paths);

    // Drops "path\x1f..." keys whose path is listed: one pass, not paths x keys.
    static void erasePathKeys(std::unordered_set<std::string> &keys,
                              const std::unordered_set<std::string> &listed);

    // Failed keys block scroll refills. A source rebuild (filter, sort, scan)
    // retries listed paths once; scheduleLocked does not clear m_failed.
    void dropFailedForPathsLocked(const std::vector<std::string> &paths);

    void replaceSourcesForConsumer(int cid, const std::vector<std::string> &paths);
    void updateSourcesForConsumer(int cid, const std::vector<std::string> &paths);
    void setVisibleRangeForConsumer(int cid, size_t begin, size_t end);

    void invalidatePath(const std::string &path);

    void setPredictiveCount(size_t n)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_predictive = n;
    }

    void setPredictiveCountForConsumer(int cid, size_t n);

    static size_t recommendedPredictiveCount(size_t directorySize, bool highLatencyIo);

    void setThumbSize(int size);

    ImageData findCached(const std::string &path, int size);
    ImageData request(const std::string &path, int size);

    void clear();

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

    static ThumbnailPipeline &instance();

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

    void pruneCacheLocked();
    void cacheLocked(const std::string &k, const std::string &path, const ImageData &data);
    bool visibleDemandSatisfiedLocked(const std::vector<std::string> &sources, size_t vb,
                                      size_t ve) const;
    void scheduleLocked(bool refillOnly);
    void enqueueLocked(const std::string &path, int cid, uint64_t slotGen,
                       TaskScheduler::Priority prio, bool refillOnly);
    // Called only while m_mtx is held.
    void dropEnqueueOwnerLocked(const std::string &k, uint64_t owner, const std::string &ownerKey);
    bool enqueueRequestStaleLocked(int cid, uint64_t slotGen, const std::string &path,
                                   uint64_t pathRevision, bool cancelled);
    bool publishEnqueueResultLocked(const std::string &k, const std::string &path, uint64_t owner,
                                    const std::string &ownerKey, int cid, uint64_t slotGen,
                                    uint64_t pathRevision, const ResultFn &result,
                                    const ImageData &thumb, ImageData &deliver, ResultFn &cb,
                                    std::vector<ResultFn> &subscribers);
    void cancelObsoleteHandlesLocked();
    void cancelHandlesForConsumerLocked(int cid);
    void cancelHandlesLocked();

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
