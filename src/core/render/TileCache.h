#pragma once

#include "core/image/ImageBuffer.h"
#include "Viewport.h"
#include "TileGrid.h"

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ─── TileCache ───────────────────────────────────────────────────────────────
// LRU cache of decoded/scaled image tiles, keyed by (imageId, col, row, lod).
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

    bool operator==(const TileKey& o) const
    {
        return imageId == o.imageId && col == o.col && row == o.row && lod == o.lod;
    }
};

struct TileKeyHash
{
    size_t operator()(const TileKey& k) const
    {
        size_t h = std::hash<std::string>()(k.imageId);
        h ^= std::hash<int>()(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.row) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.lod) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Decodes the source region (in full-res image pixels) for the given LOD into a
// scaled tile of `targetW x targetH` screen pixels. Returns null ImageData on
// failure. The callback owns the real decode (RenderEngine::scaleRegion).
using TileDecodeFn = std::function<ImageData(
    const std::string& imageId, int srcX, int srcY, int srcW, int srcH, int targetW, int targetH)>;

struct TileCache
{
    // Soft budget: number of tiles retained (LRU). Default ~1024 tiles.
    size_t maxTiles = 1024;

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
        if (scale >= 1.0)
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
        int s = baseTileSize;
        for (int i = 0; i < lod; ++i)
            s *= 2;
        return s;
    }

    // Returns the tile for (imageId, col, row, lod) if cached, else null.
    ImageData get(const TileKey& k)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_map.find(k);
        if (it == m_map.end())
            return ImageData{};
        m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
        return it->second.entry.data;
    }

    // Insert/replace a tile, enforcing the LRU budget (evicts least-recently-
    // used tiles). Thread-safe.
    void put(const TileKey& k, const ImageData& data)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_map.find(k);
        if (it != m_map.end())
        {
            it->second.entry.data = data;
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
            return;
        }
        Entry2 e;
        e.key = k;
        e.data = data;
        e.pixels = data.byteSize();
        m_lru.push_front(e);
        Node n;
        n.key = k;
        n.entry = e;
        n.lruIt = m_lru.begin();
        m_map[k] = n;
        while (m_map.size() > maxTiles)
        {
            const Entry2& back = m_lru.back();
            m_map.erase(back.key);
            m_lru.pop_back();
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_map.clear();
        m_lru.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_map.size();
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
    std::vector<ReadyTile> request(const std::string& imageId,
        const Viewport& vp,
        const TileGrid& grid,
        const TileDecodeFn& decode,
        int* decodeCalls = nullptr,
        int maxLod = 4)
    {
        std::vector<ReadyTile> out;
        const int lod = chooseLod(vp.scale, maxLod);
        const int lodSize = lodTileSize(grid.tileSize, lod);
        const TileGrid lodGrid(grid.imageW, grid.imageH, lodSize);
        auto tiles = lodGrid.visibleTiles(vp);
        for (const auto& t : tiles)
        {
            TileKey k{imageId, t.coord.col, t.coord.row, lod};
            ImageData cached = get(k);
            if (!cached.isNull())
            {
                out.push_back({k, cached});
                continue;
            }
            int sx, sy, sw, sh;
            vp.imageRectToScreen(t.srcX, t.srcY, t.srcW, t.srcH, sx, sy, sw, sh);
            ImageData decoded = decode(imageId, t.srcX, t.srcY, t.srcW, t.srcH, sw, sh);
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
    struct Entry2
    {
        TileKey key;
        ImageData data;
        size_t pixels = 0;
    };
    struct Node
    {
        TileKey key;
        Entry2 entry;
        std::list<Entry2>::iterator lruIt;
    };

    mutable std::mutex m_mtx;
    std::unordered_map<TileKey, Node, TileKeyHash> m_map;
    std::list<Entry2> m_lru;
};
