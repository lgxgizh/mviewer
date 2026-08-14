#include "core/render/AsyncTileRequestManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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
    AsyncTileRequestManager::ReadyCallback onReady;
    std::atomic<bool> cancelled{false};
    std::mutex resultMtx;
    ImageData result;
    TaskScheduler::TaskHandle handle;
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
};

namespace
{
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
        }
        if (alreadyPending)
        {
            ++result.pending;
            continue;
        }

        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [pending](const TaskScheduler::TaskContext &ctx)
            {
                if (ctx.isCancelled() || pending->cancelled.load(std::memory_order_acquire))
                    return;
                ImageData value = pending->decode(
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
        {
            std::lock_guard<std::mutex> lk(impl->mtx);
            const auto it = impl->pending.find(key);
            if (it != impl->pending.end() && it->second == pending)
                impl->pending.erase(it);
            continue;
        }
        pending->handle = handle;
        ++result.pending;
    }
    return result;
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
