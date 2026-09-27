#pragma once

#include "TileCache.h"
#include "core/render/RegionTileSelect.h"
#include "core/scheduler/TaskScheduler.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// Coordinates non-blocking tile preparation for a single image presentation.
// The manager owns the Missing -> Pending -> Ready transition while TileCache
// remains the thread-safe Ready-value store. Worker code receives only value
// snapshots and may never construct QPixmap, QWidget, or GL resources.
class AsyncTileRequestManager
{
  public:
    using ReadyCallback = std::function<void(const TileKey &)>;
    using DerivedDecodeFn = std::function<ImageData(const TileKey &, const ImageData &)>;

    struct VisibleTiles
    {
        std::vector<TileCache::ReadyTile> ready;
        size_t pending = 0;
        size_t missing = 0;

        bool complete() const
        {
            return missing == 0;
        }
    };

    explicit AsyncTileRequestManager(TileCache &cache);
    ~AsyncTileRequestManager();

    AsyncTileRequestManager(const AsyncTileRequestManager &) = delete;
    AsyncTileRequestManager &operator=(const AsyncTileRequestManager &) = delete;

    // Starts a new image/view lifetime. All requests from older generations
    // are soft-cancelled and their results are discarded even if the worker
    // was already inside a non-interruptible decode.
    void reset(uint64_t generation);

    // Returns Ready tiles immediately and schedules every Missing tile. A
    // repeated call for a Pending canonical key is de-duplicated. `decode`
    // runs on TaskScheduler::DecodePool and must be pure CPU/value work.
    VisibleTiles requestVisible(const std::string &imageId, const Viewport &viewport,
                                const TileGrid &grid, int renderScalePercent, uint64_t generation,
                                TileDecodeFn decode, ReadyCallback onReady);

    // Zoomed-in pans (scale >= 1) schedule the visible tiles plus a one-tile
    // ring. Keys match a later pan, so ready tiles are not decoded again.
    // Zoomed-out views keep the exact visible set (coarse LOD already covers
    // a wide region). The ring is part of the same requestVisible generation.
    VisibleTiles requestVisibleRegion(const std::string &imageId, const Viewport &viewport,
                                      const TileGrid &grid, int renderScalePercent,
                                      uint64_t generation, TileDecodeFn decode,
                                      ReadyCallback onReady)
    {
        const int ring = (viewport.scale >= 1.0 && std::isfinite(viewport.scale)) ? 1 : 0;
        const Viewport wide =
            mviewer::core::inflateViewportForTileRing(viewport, grid.tileSize, ring);
        return requestVisible(imageId, wide, grid, renderScalePercent, generation,
                              std::move(decode), std::move(onReady));
    }

    // Schedule a derived value (for example an overlay tile) without doing
    // the materialization in a GUI paint callback. The source is a cheap
    // ImageData value snapshot; the transform runs on the Decode pool and is
    // de-duplicated by the canonical key.
    ImageData requestDerived(const TileKey &key, const ImageData &source, uint64_t generation,
                             DerivedDecodeFn decode, ReadyCallback onReady);

    size_t pendingCount() const;
    uint64_t generation() const;

    struct Impl;

  private:
    std::shared_ptr<Impl> m_impl;
};
