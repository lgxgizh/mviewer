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
    // was already inside a non-interruptible decode. Accepts new work for
    // `generation` (unlike shutdown).
    void reset(uint64_t generation);

    // Stop accepting work and cancel pending tiles. Used when the viewer is
    // going away so a late worker cannot deliver into a destroyed widget.
    // `reset` re-opens acceptance for a new generation.
    void shutdown();

    // Returns Ready tiles immediately and schedules every Missing tile. A
    // repeated call for a Pending canonical key is de-duplicated. On-screen
    // tiles use Decode; a lower priority is for ring prefetch only. `decode`
    // must be pure CPU/value work.
    VisibleTiles requestVisible(const std::string &imageId, const Viewport &viewport,
                                const TileGrid &grid, int renderScalePercent, uint64_t generation,
                                const TileDecodeFn &decode, const ReadyCallback &onReady,
                                TaskScheduler::Priority priority = TaskScheduler::Priority::Decode);

    // Zoomed-in pans (scale >= 1) schedule visible tiles at Decode, then a
    // capped one-tile ring at Background. Zoomed-out views keep the exact
    // visible set. Returned counts cover the on-screen set only, so a pan is
    // not blocked on the ring. Ring keys match a later pan.
    VisibleTiles requestVisibleRegion(const std::string &imageId, const Viewport &viewport,
                                      const TileGrid &grid, int renderScalePercent,
                                      uint64_t generation, const TileDecodeFn &decode,
                                      const ReadyCallback &onReady);

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
