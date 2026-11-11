#include "core/thumbnail/ThumbnailPipeline.h"

#include <algorithm>

ThumbnailPipeline::ResultToken ThumbnailPipeline::addResultFn(ResultFn fn)
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

void ThumbnailPipeline::removeResultFn(ResultToken token)
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

void ThumbnailPipeline::setSourcesForConsumer(int cid, const std::vector<std::string> &paths)
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
        const std::unordered_set<std::string> listed(paths.begin(), paths.end());
        erasePathKeys(m_failed, listed);
        erasePathKeys(m_attemptedKeys, listed);
    }
    cancelObsoleteHandlesLocked();
}

void ThumbnailPipeline::appendSourcesForConsumer(int cid, const std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    ConsumerSlot &slot = m_consumers[cid];
    slot.sources.insert(slot.sources.end(), paths.begin(), paths.end());
    if (cid == 0)
        m_sources = slot.sources;
}

void ThumbnailPipeline::erasePathKeys(std::unordered_set<std::string> &keys,
                                      const std::unordered_set<std::string> &listed)
{
    std::erase_if(keys,
                  [&listed](const std::string &key)
                  {
                      const auto sep = key.find('\x1f');
                      return sep != std::string::npos && listed.count(key.substr(0, sep)) != 0;
                  });
}

void ThumbnailPipeline::dropFailedForPathsLocked(const std::vector<std::string> &paths)
{
    if (m_failed.empty())
        return;
    const std::unordered_set<std::string> listed(paths.begin(), paths.end());
    erasePathKeys(m_failed, listed);
}

void ThumbnailPipeline::replaceSourcesForConsumer(int cid, const std::vector<std::string> &paths)
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

void ThumbnailPipeline::updateSourcesForConsumer(int cid, const std::vector<std::string> &paths)
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

void ThumbnailPipeline::setVisibleRangeForConsumer(int cid, size_t begin, size_t end)
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

void ThumbnailPipeline::invalidatePath(const std::string &path)
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

void ThumbnailPipeline::setPredictiveCountForConsumer(int cid, size_t n)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_consumers.find(cid);
    if (it != m_consumers.end())
        it->second.predictive = n;
    else
        m_predictive = n;
}

size_t ThumbnailPipeline::recommendedPredictiveCount(size_t directorySize, bool highLatencyIo)
{
    if (highLatencyIo)
        return directorySize > 500 ? 12 : 8;
    if (directorySize > 2000)
        return 96;
    if (directorySize > 500)
        return 64;
    return 48;
}

void ThumbnailPipeline::setThumbSize(int size)
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

ImageData ThumbnailPipeline::findCached(const std::string &path, int size)
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

ImageData ThumbnailPipeline::request(const std::string &path, int size)
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

void ThumbnailPipeline::clear()
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

ThumbnailPipeline &ThumbnailPipeline::instance()
{
    static ThumbnailPipeline inst;
    return inst;
}

void ThumbnailPipeline::pruneCacheLocked()
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

void ThumbnailPipeline::cacheLocked(const std::string &k, const std::string &path,
                                    const ImageData &data)
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

bool ThumbnailPipeline::visibleDemandSatisfiedLocked(const std::vector<std::string> &sources,
                                                     size_t vb, size_t ve) const
{
    for (size_t i = vb; i < ve; ++i)
    {
        const std::string k = key(sources[i], thumbSize);
        if (m_memCache.count(k) == 0 && m_failed.count(k) == 0)
            return false;
    }
    return true;
}

void ThumbnailPipeline::scheduleLocked(bool refillOnly)
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
                if (m_memCache.count(k) != 0 || m_failed.count(k) != 0 || m_pending.count(k) != 0)
                    continue;
                enqueueLocked(slot.sources[i], cid, slot.gen, TaskScheduler::Priority::Thumbnail,
                              refillOnly);
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

void ThumbnailPipeline::dropEnqueueOwnerLocked(const std::string &k, uint64_t owner,
                                               const std::string &ownerKey)
{
    m_handles.erase(ownerKey);
    auto pit = m_pending.find(k);
    if (pit != m_pending.end() && pit->second.owner == owner)
        m_pending.erase(pit);
}

bool ThumbnailPipeline::enqueueRequestStaleLocked(int cid, uint64_t slotGen,
                                                  const std::string &path, uint64_t pathRevision,
                                                  bool cancelled)
{
    auto it = m_consumers.find(cid);
    return cancelled || m_pathRevisions[path] != pathRevision ||
           (it != m_consumers.end() && it->second.gen != slotGen) ||
           (it == m_consumers.end() && cid == 0 && m_gen != slotGen);
}

bool ThumbnailPipeline::publishEnqueueResultLocked(const std::string &k, const std::string &path,
                                                   uint64_t owner, const std::string &ownerKey,
                                                   int cid, uint64_t slotGen, uint64_t pathRevision,
                                                   const ResultFn &result, const ImageData &thumb,
                                                   ImageData &deliver, ResultFn &cb,
                                                   std::vector<ResultFn> &subscribers)
{
    m_handles.erase(ownerKey);
    auto pit = m_pending.find(k);
    if (pit == m_pending.end() || pit->second.owner != owner)
        return false;
    m_pending.erase(pit);
    auto it = m_consumers.find(cid);
    if (it != m_consumers.end() && it->second.gen != slotGen)
        return false;
    if (it == m_consumers.end() && cid == 0 && m_gen != slotGen)
        return false;
    if (m_pathRevisions[path] != pathRevision)
        return false;
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
    return true;
}

void ThumbnailPipeline::enqueueLocked(const std::string &path, int cid, uint64_t slotGen,
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
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                if (enqueueRequestStaleLocked(cid, slotGen, path, pathRevision, ctx.isCancelled()))
                {
                    dropEnqueueOwnerLocked(k, owner, ownerKey);
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
                if (!publishEnqueueResultLocked(k, path, owner, ownerKey, cid, slotGen,
                                                pathRevision, result, thumb, deliver, cb,
                                                subscribers))
                    return;
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

void ThumbnailPipeline::cancelObsoleteHandlesLocked()
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

void ThumbnailPipeline::cancelHandlesForConsumerLocked(int cid)
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

void ThumbnailPipeline::cancelHandlesLocked()
{
    for (auto &kv : m_handles)
        TaskScheduler::cancel(kv.second);
    m_handles.clear();
}
