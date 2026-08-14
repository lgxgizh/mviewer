#pragma once

#include "TileGrid.h"
#include "Viewport.h"
#include "core/image/ImageBuffer.h"

#include <cstdint>
#include <cmath>
#include <limits>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ─── TileCache ───────────────────────────────────────────────────────────────
// LRU cache of canonical decoded/scaled image tiles, keyed by
// (imageId, col, row, lod, renderScalePercent).
// This is the memory tier of the Render Pipeline: the Widget never decodes a
// whole image per paint — it asks TileCache for the visible tiles at the LOD
// chosen for the current zoom, and only missing tiles are decoded (and cached).
//
// LOD: when zoomed out, one coarse tile covers a larger source region, so fit-
// to-window on a 100 MP / RAW image decodes a few coarse tiles instead of the
// full bitmap. (Direct disk-LOD decode — i.e. the Decoder emitting a reduced
// resolution bitmap — is a later milestone; this cache tiles the already-
// decoded ImageFrame. The LOD *selection* math is real and exercised here.)
//
// Decode is injected as a callback so the cache is unit-testable without a
// display and without coupling to RenderEngine in tests.

struct TileKey
{
    std::string imageId;
    int col = 0;
    int row = 0;
    int lod = 0;
    // Canonical output-resolution policy in percent of the base tile pixel
    // size.  Transient viewport zoom is deliberately not part of the key;
    // display scale is applied by the compositor.  DPR belongs here because
    // it changes the materialized payload (100% and 150% tiles are distinct,
    // stable payloads rather than one key with two possible resolutions).
    int renderScalePercent = 100;

    bool operator==(const TileKey &o) const
    {
        return imageId == o.imageId && col == o.col && row == o.row && lod == o.lod &&
               renderScalePercent == o.renderScalePercent;
    }
};

struct TileKeyHash
{
    size_t operator()(const TileKey &k) const
    {
        size_t h = std::hash<std::string>()(k.imageId);
        h ^= std::hash<int>()(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.row) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.lod) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.renderScalePercent) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Decodes the source region (in full-res image pixels) for the given LOD into a
// scaled tile of `targetW x targetH` screen pixels. Returns null ImageData on
// failure. The callback owns the real decode (RenderEngine::scaleRegion).
using TileDecodeFn = std::function<ImageData(const std::string &imageId, int srcX, int srcY,
                                             int srcW, int srcH, int targetW, int targetH)>;

struct TileCache
{
    // Memory budget is authoritative.  maxTiles remains as an optional
    // compatibility guard for callers that explicitly want an entry cap; it
    // is not the default resource policy.
    size_t maxBytes = 64 * 1024 * 1024;
    size_t maxTiles = std::numeric_limits<size_t>::max();

    struct Entry
    {
        TileKey key;
        ImageData data;
        size_t pixels = 0;
    };

    // Choose the LOD level for a viewport scale. LOD 0 = full-res tiles
    // (tileSize source px each). Each LOD step doubles the source region per
    // tile, so higher LOD = coarser. Scale < 1 (zoomed out) -> higher LOD.
    static int chooseLod(double scale, int maxLod = 4)
    {
        if (!(scale > 0.0) || !std::isfinite(scale) || scale >= 1.0)
            return 0;
        // scale = screen_px / src_px. A tile of tileSize src px maps to
        // tileSize*scale screen px. We want a tile to cover ~tileSize screen px
        // at this zoom, so the source region per tile should be ~tileSize/scale.
        // lod ~ log2(1/scale).
        double lod = std::log2(1.0 / scale);
        int l = static_cast<int>(std::ceil(lod));
        if (l < 0)
            l = 0;
        if (l > maxLod)
            l = maxLod;
        return l;
    }

    // Source pixel size of a tile at the given LOD (tileSize * 2^lod).
    static int lodTileSize(int baseTileSize, int lod)
    {
        if (baseTileSize <= 0)
            return 0;
        int s = baseTileSize;
        for (int i = 0; i < lod; ++i)
        {
            if (s > std::numeric_limits<int>::max() / 2)
                return std::numeric_limits<int>::max();
            s *= 2;
        }
        return s;
    }

    // Canonical payload size for one source extent.  A tile at LOD N covers
    // 2^N source pixels per output pixel, while renderScalePercent accounts
    // for the device-pixel policy.  The result is independent of continuous
    // viewport zoom, so a key never aliases two resolutions.
    static int canonicalTilePixels(int sourceExtent, int baseTileSize, int lod,
                                   int renderScalePercent = 100)
    {
        if (sourceExtent <= 0 || baseTileSize <= 0)
            return 0;
        const int safeLod = std::max(0, lod);
        const int reduction = safeLod >= 30 ? (1 << 30) : (1 << safeLod);
        const int scale = std::max(1, renderScalePercent);
        const int64_t numerator = static_cast<int64_t>(sourceExtent) * scale;
        const int64_t denominator = static_cast<int64_t>(reduction) * 100;
        const int64_t pixels = (numerator + denominator - 1) / denominator;
        return static_cast<int>(std::clamp<int64_t>(
            pixels, 1, std::numeric_limits<int>::max()));
    }

    // Returns the tile for (imageId, col, row, lod) if cached, else null.
    ImageData get(const TileKey &k)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_map.find(k);
        if (it == m_map.end())
        {
            ++m_misses;
            return ImageData{};
        }
        ++m_hits;
        m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
        return it->second.data;
    }

    // Insert/replace a tile, enforcing the LRU budget (evicts least-recently-
    // used tiles). Thread-safe.
    void put(const TileKey &k, const ImageData &data)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const size_t bytes = data.byteSize();
        auto it = m_map.find(k);
        if (it != m_map.end())
        {
            m_bytes -= it->second.bytes;
            if (bytes == 0 || bytes > maxBytes)
            {
                m_lru.erase(it->second.lruIt);
                m_map.erase(it);
                return;
            }
            it->second.data = data;
            it->second.bytes = bytes;
            m_bytes += bytes;
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
            evictLocked();
            return;
        }
        if (bytes == 0 || bytes > maxBytes)
            return; // explicit policy: one oversized tile is never retained
        m_lru.push_front(k);
        Node n;
        n.data = data;
        n.bytes = bytes;
        n.lruIt = m_lru.begin();
        m_map[k] = n;
        m_bytes += bytes;
        evictLocked();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_map.clear();
        m_lru.clear();
        m_bytes = 0;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_map.size();
    }

    size_t byteUsage() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_bytes;
    }

    struct Metrics
    {
        size_t tileCount = 0;
        size_t bytes = 0;
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t evictions = 0;
    };

    Metrics metrics() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return {m_map.size(), m_bytes, m_hits, m_misses, m_evictions};
    }

    // Request all visible tiles for `grid` at the LOD chosen for `vp`. Cached
    // tiles are returned directly; missing tiles are produced via `decode` and
    // cached. `decodeCalls` (if non-null) counts how many tiles had to be
    // decoded (for tests / instrumentation). The returned entries are in
    // paint order with their on-screen rect already computed by the caller.
    struct ReadyTile
    {
        TileKey key;
        ImageData data;
    };
    std::vector<ReadyTile> request(const std::string &imageId, const Viewport &vp,
                                   const TileGrid &grid, const TileDecodeFn &decode,
                                   int *decodeCalls = nullptr, int maxLod = 4,
                                   int renderScalePercent = 100)
    {
        std::vector<ReadyTile> out;
        const int lod = chooseLod(vp.scale, maxLod);
        const int lodSize = lodTileSize(grid.tileSize, lod);
        const TileGrid lodGrid(grid.imageW, grid.imageH, lodSize);
        auto tiles = lodGrid.visibleTiles(vp);
        for (const auto &t : tiles)
        {
            TileKey k{imageId, t.coord.col, t.coord.row, lod,
                      std::max(1, renderScalePercent)};
            ImageData cached = get(k);
            if (!cached.isNull())
            {
                out.push_back({k, cached});
                continue;
            }
            const int targetW = canonicalTilePixels(t.srcW, grid.tileSize, lod,
                                                    k.renderScalePercent);
            const int targetH = canonicalTilePixels(t.srcH, grid.tileSize, lod,
                                                    k.renderScalePercent);
            ImageData decoded = decode(imageId, t.srcX, t.srcY, t.srcW, t.srcH, targetW, targetH);
            if (decoded.isNull())
                continue;
            put(k, decoded);
            if (decodeCalls)
                ++(*decodeCalls);
            out.push_back({k, decoded});
        }
        return out;
    }

  private:
    struct Node
    {
        ImageData data;
        size_t bytes = 0;
        std::list<TileKey>::iterator lruIt;
    };

    void evictLocked()
    {
        while ((m_map.size() > maxTiles || m_bytes > maxBytes) && !m_lru.empty())
        {
            const TileKey key = m_lru.back();
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                m_bytes -= it->second.bytes;
                m_map.erase(it);
                ++m_evictions;
            }
            m_lru.pop_back();
        }
    }

    mutable std::mutex m_mtx;
    std::unordered_map<TileKey, Node, TileKeyHash> m_map;
    std::list<TileKey> m_lru;
    size_t m_bytes = 0;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    uint64_t m_evictions = 0;
};
