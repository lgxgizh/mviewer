#include "core/render/AsyncTileRequestManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
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
    }
    const std::weak_ptr<AsyncTileRequestManager::Impl> weakImpl = impl;
    std::thread([weakImpl, delay]()
                {
                    std::this_thread::sleep_for(delay);
                    const auto impl = weakImpl.lock();
                    if (!impl)
                        return;

                    std::vector<std::shared_ptr<PendingTile>> candidates;
                    {
                        std::lock_guard<std::mutex> lk(impl->mtx);
                        impl->retryScheduled = false;
                        if (!impl->accepting)
                            return;
                        candidates.reserve(32);
                        for (const auto &[key, pending] : impl->pending)
                        {
                            (void)key;
                            if (!pending->handle &&
                                !pending->cancelled.load(std::memory_order_acquire))
                                candidates.push_back(pending);
                            if (candidates.size() == 32)
                                break;
                        }
                    }

                    bool rejected = false;
                    for (const auto &pending : candidates)
                        if (!submitPending(impl, pending))
                            rejected = true;

                    bool stillPending = false;
                    {
                        std::lock_guard<std::mutex> lk(impl->mtx);
                        for (const auto &[key, pending] : impl->pending)
                        {
                            (void)key;
                            if (!pending->handle &&
                                !pending->cancelled.load(std::memory_order_acquire))
                            {
                                stillPending = true;
                                break;
                            }
                        }
                    }
                    if (stillPending)
                        scheduleRetry(impl, rejected ? std::chrono::milliseconds(100)
                                                     : std::chrono::milliseconds(50));
                })
        .detach();
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
                value = pending->decode(
                    pending->key.imageId, pending->srcX, pending->srcY, pending->srcW,
                    pending->srcH, pending->targetW, pending->targetH);
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
}

AsyncTileRequestManager::~AsyncTileRequestManager()
{
    const auto impl = m_impl;
    std::vector<std::shared_ptr<PendingTile>> stale;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        impl->accepting = false;
        impl->retryScheduled = false;
        for (auto &[key, request] : impl->pending)
        {
            (void)key;
            stale.push_back(std::move(request));
        }
        impl->pending.clear();
    }
    for (const auto &request : stale)
    {
        request->cancelled.store(true, std::memory_order_release);
        if (request->handle)
            TaskScheduler::cancel(request->handle);
    }
}

void AsyncTileRequestManager::reset(uint64_t generation)
{
    const auto impl = m_impl;
    std::vector<std::shared_ptr<PendingTile>> stale;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        impl->generation = generation;
        for (auto &[key, request] : impl->pending)
        {
            (void)key;
            stale.push_back(std::move(request));
        }
        impl->pending.clear();
    }
    for (const auto &request : stale)
    {
        request->cancelled.store(true, std::memory_order_release);
        if (request->handle)
            TaskScheduler::cancel(request->handle);
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
    for (const auto &tile : lodGrid.visibleTiles(viewport))
    {
        const TileKey key{imageId, tile.coord.col, tile.coord.row, lod, policy};
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
    std::vector<std::shared_ptr<PendingTile>> evicted;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        if (impl->pending.size() > kMaxPendingTiles)
        {
            std::unordered_map<TileKey, bool, TileKeyHash> visibleKeys;
            visibleKeys.reserve(result.ready.size() + result.missing);
            for (const auto &tile : lodGrid.visibleTiles(viewport))
                visibleKeys.emplace(TileKey{imageId, tile.coord.col, tile.coord.row, lod, policy},
                                    true);
            while (impl->pending.size() > kMaxPendingTiles)
            {
                auto victim = impl->pending.end();
                for (auto it = impl->pending.begin(); it != impl->pending.end(); ++it)
                {
                    if (visibleKeys.count(it->first) != 0)
                        continue;
                    if (victim == impl->pending.end() || it->second->serial < victim->second->serial)
                        victim = it;
                }
                if (victim == impl->pending.end())
                    break;
                victim->second->cancelled.store(true, std::memory_order_release);
                evicted.push_back(victim->second);
                impl->pending.erase(victim);
            }
        }
    }
    for (const auto &request : evicted)
        if (request->handle)
            TaskScheduler::cancel(request->handle);
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
    std::vector<std::shared_ptr<PendingTile>> evicted;
    {
        std::lock_guard<std::mutex> lk(impl->mtx);
        const auto [it, inserted] = impl->pending.emplace(key, pending);
        if (!inserted)
            return {};
        pending->serial = ++impl->nextSerial;
        while (impl->pending.size() > kMaxPendingTiles)
        {
            auto victim = impl->pending.end();
            for (auto candidate = impl->pending.begin(); candidate != impl->pending.end();
                 ++candidate)
            {
                if (candidate->second == pending)
                    continue;
                if (victim == impl->pending.end() ||
                    candidate->second->serial < victim->second->serial)
                    victim = candidate;
            }
            if (victim == impl->pending.end())
                break;
            victim->second->cancelled.store(true, std::memory_order_release);
            evicted.push_back(victim->second);
            impl->pending.erase(victim);
        }
    }
    for (const auto &request : evicted)
        if (request->handle)
            TaskScheduler::cancel(request->handle);
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
