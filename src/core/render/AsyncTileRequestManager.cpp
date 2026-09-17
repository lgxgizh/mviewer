#include "core/render/AsyncTileRequestManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
struct PendingTile
{
    TileKey key;
    uint64_t generation = 0;
    int srcX = 0;
    int srcY = 0;
    int srcW = 0;
    int srcH = 0;
    int targetW = 0;
    int targetH = 0;
    TileDecodeFn decode;
    ImageData source;
    AsyncTileRequestManager::DerivedDecodeFn derivedDecode;
    AsyncTileRequestManager::ReadyCallback onReady;
    std::atomic<bool> cancelled{false};
    std::mutex resultMtx;
    ImageData result;
    TaskScheduler::TaskHandle handle;
    uint64_t serial = 0;
};
} // namespace

struct AsyncTileRequestManager::Impl
{
    explicit Impl(TileCache &cacheRef) : cache(&cacheRef)
    {
    }

    TileCache *cache = nullptr;
    mutable std::mutex mtx;
    uint64_t generation = 0;
    bool accepting = true;
    std::unordered_map<TileKey, std::shared_ptr<PendingTile>, TileKeyHash> pending;
    uint64_t nextSerial = 0;
    bool retryScheduled = false;
    bool retryStop = false;
    std::chrono::milliseconds retryDelay{50};
    std::condition_variable_any retryCv;
    std::jthread retryWorker;
};

namespace
{
constexpr size_t kMaxPendingTiles = 256;

void finishPending(const std::shared_ptr<AsyncTileRequestManager::Impl> &impl,
                   const std::shared_ptr<PendingTile> &pending);

bool submitPending(const std::shared_ptr<AsyncTileRequestManager::Impl> &impl,
                   const std::shared_ptr<PendingTile> &pending);

void scheduleRetry(const std::shared_ptr<AsyncTileRequestManager::Impl> &impl,
                   std::chrono::milliseconds delay = std::chrono::milliseconds(50))
{
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        if (!impl->accepting || impl->pending.empty() || impl->retryScheduled)
            return;
        impl->retryScheduled = true;
        impl->retryDelay = delay;
    }
    impl->retryCv.notify_one();
}

bool submitPending(const std::shared_ptr<AsyncTileRequestManager::Impl> &impl,
                   const std::shared_ptr<PendingTile> &pending)
{
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        const auto it = impl->pending.find(pending->key);
        if (!impl->accepting || it == impl->pending.end() || it->second != pending ||
            pending->cancelled.load(std::memory_order_acquire) || pending->handle)
            return true;
    }

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [pending](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled() || pending->cancelled.load(std::memory_order_acquire))
                return;
            ImageData value;
            if (pending->derivedDecode)
                value = pending->derivedDecode(pending->key, pending->source);
            else
                value = pending->decode(pending->key.imageId, pending->srcX, pending->srcY,
                                        pending->srcW, pending->srcH, pending->targetW,
                                        pending->targetH);
            if (ctx.isCancelled() || pending->cancelled.load(std::memory_order_acquire))
                return;
            std::lock_guard<std::mutex> lk(pending->resultMtx);
            pending->result = std::move(value);
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [impl, pending]() { finishPending(impl, pending); });

    if (!handle)
        return false;

    bool keep = false;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        const auto it = impl->pending.find(pending->key);
        if (impl->accepting && it != impl->pending.end() && it->second == pending &&
            !pending->cancelled.load(std::memory_order_acquire))
        {
            pending->handle = handle;
            keep = true;
        }
    }
    if (!keep)
        TaskScheduler::cancel(handle);
    return true;
}

void retryLoop(const std::weak_ptr<AsyncTileRequestManager::Impl> &weakImpl,
               std::stop_token stopToken)
{
    for (;;)
    {
        const auto impl = weakImpl.lock();
        if (!impl)
            return;

        std::unique_lock<std::mutex> lk(impl->mtx);
        impl->retryCv.wait(
            lk, [&]()
            { return stopToken.stop_requested() || impl->retryStop || impl->retryScheduled; });
        if (stopToken.stop_requested() || impl->retryStop)
            return;

        const auto delay = impl->retryDelay;
        // reset() cancels a pending retry by clearing retryScheduled and
        // notifying this wait. Destruction uses retryStop/request_stop, so the
        // worker never makes teardown wait for a blind sleep interval.
        if (impl->retryCv.wait_for(
                lk, delay, [&]()
                { return stopToken.stop_requested() || impl->retryStop || !impl->retryScheduled; }))
            continue;

        impl->retryScheduled = false;
        if (!impl->accepting)
            continue;

        std::vector<std::shared_ptr<PendingTile>> candidates;
        candidates.reserve(32);
        for (const auto &[key, pending] : impl->pending)
        {
            (void)key;
            if (!pending->handle && !pending->cancelled.load(std::memory_order_acquire))
                candidates.push_back(pending);
        }
        if (candidates.size() > 32)
        {
            std::partial_sort(candidates.begin(), candidates.begin() + 32, candidates.end(),
                              [](const auto &a, const auto &b) { return a->serial < b->serial; });
            candidates.resize(32);
        }
        lk.unlock();

        bool rejected = false;
        for (const auto &pending : candidates)
            if (!submitPending(impl, pending))
                rejected = true;

        lk.lock();
        bool stillPending = false;
        for (const auto &[key, pending] : impl->pending)
        {
            (void)key;
            if (!pending->handle && !pending->cancelled.load(std::memory_order_acquire))
            {
                stillPending = true;
                break;
            }
        }
        lk.unlock();
        if (stillPending)
            scheduleRetry(impl, rejected ? std::chrono::milliseconds(100)
                                         : std::chrono::milliseconds(50));
    }
}

void finishPending(const std::shared_ptr<AsyncTileRequestManager::Impl> &impl,
                   const std::shared_ptr<PendingTile> &pending)
{
    ImageData result;
    {
        std::lock_guard<std::mutex> lk(pending->resultMtx);
        result = std::move(pending->result);
    }

    AsyncTileRequestManager::ReadyCallback callback;
    bool deliver = false;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        const auto it = impl->pending.find(pending->key);
        if (it == impl->pending.end() || it->second != pending)
            return;
        impl->pending.erase(it);
        if (impl->accepting && !pending->cancelled.load(std::memory_order_acquire) &&
            pending->generation == impl->generation && !result.isNull())
        {
            impl->cache->put(pending->key, result);
            callback = std::move(pending->onReady);
            deliver = true;
        }
    }

    // Never invoke a client callback while holding the manager mutex. The UI
    // adapter normally turns this into one coalesced queued repaint.
    if (deliver && callback)
        callback(pending->key);
}
} // namespace

AsyncTileRequestManager::AsyncTileRequestManager(TileCache &cache)
    : m_impl(std::make_shared<Impl>(cache))
{
    const std::weak_ptr<Impl> weakImpl = m_impl;
    m_impl->retryWorker = std::jthread([weakImpl](std::stop_token stopToken)
                                       { retryLoop(weakImpl, std::move(stopToken)); });
}

AsyncTileRequestManager::~AsyncTileRequestManager()
{
    const auto impl = m_impl;
    std::vector<TaskScheduler::TaskHandle> stale;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        impl->accepting = false;
        impl->retryScheduled = false;
        impl->retryStop = true;
        for (auto &[key, request] : impl->pending)
        {
            (void)key;
            request->cancelled.store(true, std::memory_order_release);
            if (request->handle)
                stale.push_back(std::move(request->handle));
        }
        impl->pending.clear();
    }
    impl->retryWorker.request_stop();
    impl->retryCv.notify_all();
    if (impl->retryWorker.joinable())
        impl->retryWorker.join();
    for (auto &handle : stale)
    {
        TaskScheduler::cancel(handle);
    }
}

void AsyncTileRequestManager::reset(uint64_t generation)
{
    const auto impl = m_impl;
    std::vector<TaskScheduler::TaskHandle> stale;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        impl->generation = generation;
        impl->retryScheduled = false;
        for (auto &[key, request] : impl->pending)
        {
            (void)key;
            request->cancelled.store(true, std::memory_order_release);
            if (request->handle)
                stale.push_back(std::move(request->handle));
        }
        impl->pending.clear();
    }
    impl->retryCv.notify_all();
    for (auto &handle : stale)
    {
        TaskScheduler::cancel(handle);
    }
}

AsyncTileRequestManager::VisibleTiles AsyncTileRequestManager::requestVisible(
    const std::string &imageId, const Viewport &viewport, const TileGrid &grid,
    int renderScalePercent, uint64_t generation, TileDecodeFn decode, ReadyCallback onReady)
{
    VisibleTiles result;
    if (!decode)
        return result;

    const auto impl = m_impl;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        if (!impl->accepting || impl->generation != generation)
            return result;
    }

    const int lod = TileCache::chooseLod(viewport.scale);
    const int lodSize = TileCache::lodTileSize(grid.tileSize, lod);
    const TileGrid lodGrid(grid.imageW, grid.imageH, lodSize);
    const int policy = std::max(1, renderScalePercent);
    const auto visible = lodGrid.visibleTiles(viewport);

    std::unordered_set<TileKey, TileKeyHash> visibleKeys;
    visibleKeys.reserve(visible.size());

    for (const auto &tile : visible)
    {
        const TileKey key{imageId, tile.coord.col, tile.coord.row, lod, policy};
        visibleKeys.insert(key);

        const ImageData cached = impl->cache->get(key);
        if (!cached.isNull())
        {
            result.ready.push_back({key, cached});
            continue;
        }

        ++result.missing;
        bool alreadyPending = false;
        auto pending = std::make_shared<PendingTile>();
        pending->key = key;
        pending->generation = generation;
        pending->srcX = tile.srcX;
        pending->srcY = tile.srcY;
        pending->srcW = tile.srcW;
        pending->srcH = tile.srcH;
        pending->targetW = TileCache::canonicalTilePixels(tile.srcW, grid.tileSize, lod, policy);
        pending->targetH = TileCache::canonicalTilePixels(tile.srcH, grid.tileSize, lod, policy);
        pending->decode = decode;
        pending->onReady = onReady;
        {
            std::lock_guard<std::mutex> lk(impl->mtx);
            if (impl->generation != generation || !impl->accepting)
                break;
            const auto [it, inserted] = impl->pending.emplace(key, pending);
            if (!inserted)
            {
                (void)it;
                alreadyPending = true;
            }
            else
            {
                pending->serial = ++impl->nextSerial;
            }
        }
        if (alreadyPending)
        {
            ++result.pending;
            continue;
        }

        if (!submitPending(impl, pending))
            scheduleRetry(impl);
        ++result.pending;
    }

    // Repeated pan/zoom keeps useful in-flight work, but obsolete work is
    // bounded. Only the oldest non-visible requests are cancelled; visible
    // requests and recently-created work remain eligible for reuse.
    std::vector<TaskScheduler::TaskHandle> evicted;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        if (impl->pending.size() > kMaxPendingTiles)
        {
            struct Candidate
            {
                TileKey key;
                uint64_t serial = 0;
                std::shared_ptr<PendingTile> pending;
            };
            std::vector<Candidate> nonVisible;
            nonVisible.reserve(impl->pending.size());
            for (const auto &[k, p] : impl->pending)
            {
                if (visibleKeys.find(k) == visibleKeys.end())
                    nonVisible.push_back({k, p->serial, p});
            }
            const size_t excess = impl->pending.size() - kMaxPendingTiles;
            const size_t evictCount = std::min(excess, nonVisible.size());
            if (evictCount > 0)
            {
                std::partial_sort(nonVisible.begin(),
                                  nonVisible.begin() + static_cast<std::ptrdiff_t>(evictCount),
                                  nonVisible.end(), [](const Candidate &a, const Candidate &b)
                                  { return a.serial < b.serial; });
                for (size_t i = 0; i < evictCount; ++i)
                {
                    const auto &c = nonVisible[i];
                    c.pending->cancelled.store(true, std::memory_order_release);
                    if (c.pending->handle)
                        evicted.push_back(std::move(c.pending->handle));
                    impl->pending.erase(c.key);
                }
            }
        }
    }
    for (auto &handle : evicted)
        TaskScheduler::cancel(handle);
    return result;
}

ImageData AsyncTileRequestManager::requestDerived(const TileKey &key, const ImageData &source,
                                                  uint64_t generation, DerivedDecodeFn decode,
                                                  ReadyCallback onReady)
{
    if (source.isNull() || !decode)
        return {};
    const auto impl = m_impl;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        if (!impl->accepting || impl->generation != generation)
            return {};
    }
    const ImageData cached = impl->cache->get(key);
    if (!cached.isNull())
        return cached;

    auto pending = std::make_shared<PendingTile>();
    pending->key = key;
    pending->generation = generation;
    pending->source = source;
    pending->derivedDecode = std::move(decode);
    pending->onReady = std::move(onReady);
    std::vector<TaskScheduler::TaskHandle> evicted;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        const auto [it, inserted] = impl->pending.emplace(key, pending);
        if (!inserted)
            return {};
        pending->serial = ++impl->nextSerial;
        if (impl->pending.size() > kMaxPendingTiles)
        {
            struct Candidate
            {
                TileKey key;
                uint64_t serial = 0;
                std::shared_ptr<PendingTile> pending;
            };
            std::vector<Candidate> evictable;
            evictable.reserve(impl->pending.size());
            for (const auto &[k, p] : impl->pending)
            {
                if (p != pending)
                    evictable.push_back({k, p->serial, p});
            }
            const size_t excess = impl->pending.size() - kMaxPendingTiles;
            const size_t evictCount = std::min(excess, evictable.size());
            if (evictCount > 0)
            {
                std::partial_sort(evictable.begin(),
                                  evictable.begin() + static_cast<std::ptrdiff_t>(evictCount),
                                  evictable.end(), [](const Candidate &a, const Candidate &b)
                                  { return a.serial < b.serial; });
                for (size_t i = 0; i < evictCount; ++i)
                {
                    const auto &c = evictable[i];
                    c.pending->cancelled.store(true, std::memory_order_release);
                    if (c.pending->handle)
                        evicted.push_back(std::move(c.pending->handle));
                    impl->pending.erase(c.key);
                }
            }
        }
    }
    for (auto &handle : evicted)
        TaskScheduler::cancel(handle);
    if (!submitPending(impl, pending))
        scheduleRetry(impl);
    return {};
}

size_t AsyncTileRequestManager::pendingCount() const
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    return m_impl->pending.size();
}

uint64_t AsyncTileRequestManager::generation() const
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    return m_impl->generation;
}
