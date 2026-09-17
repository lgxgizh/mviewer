#pragma once

#include "Viewport.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// ─── TileGrid ────────────────────────────────────────────────────────────────
// Divides a source image into fixed-size tiles and enumerates which tiles are
// visible for a given Viewport. The Renderer draws only visible tiles via
// RenderEngine::scaleRegion, so a 100MP / RAW image is rasterized a tile at a
// time instead of in one giant bitmap. Domain-free (std only).

struct TileCoord
{
    int col = 0;
    int row = 0;
};

struct Tile
{
    TileCoord coord;
    int srcX = 0; // source-image pixel origin
    int srcY = 0;
    int srcW = 0; // source tile size (clamped at image edge)
    int srcH = 0;
};

struct TileGrid
{
    int imageW = 0;
    int imageH = 0;
    int tileSize = 256;

    TileGrid() = default;
    TileGrid(int iw, int ih, int ts)
        : imageW(std::max(0, iw)), imageH(std::max(0, ih)), tileSize(ts > 0 ? ts : 256)
    {
    }

    int cols() const
    {
        return imageW > 0
                   ? static_cast<int>((static_cast<int64_t>(imageW) + tileSize - 1) / tileSize)
                   : 0;
    }
    int rows() const
    {
        return imageH > 0
                   ? static_cast<int>((static_cast<int64_t>(imageH) + tileSize - 1) / tileSize)
                   : 0;
    }

    // Enumerate visible tiles for the given viewport (clamped to image bounds).
    std::vector<Tile> visibleTiles(const Viewport &vp) const
    {
        std::vector<Tile> out;
        if (imageW <= 0 || imageH <= 0)
            return out;

        int vx, vy, vw, vh;
        vp.visibleImageRect(imageW, imageH, vx, vy, vw, vh);
        if (vw <= 0 || vh <= 0)
            return out;

        const int c0 = std::max(0, vx / tileSize);
        const int r0 = std::max(0, vy / tileSize);
        const int c1 = std::max(c0, (vx + vw - 1) / tileSize);
        const int r1 = std::max(r0, (vy + vh - 1) / tileSize);

        const int numRows = std::max(0, std::min(r1, rows() - 1) - r0 + 1);
        const int numCols = std::max(0, std::min(c1, cols() - 1) - c0 + 1);
        out.reserve(static_cast<size_t>(numRows) * static_cast<size_t>(numCols));

        for (int r = r0; r <= r1 && r < rows(); ++r)
        {
            for (int c = c0; c <= c1 && c < cols(); ++c)
            {
                Tile t;
                t.coord = {c, r};
                t.srcX = c * tileSize;
                t.srcY = r * tileSize;
                t.srcW = std::min(tileSize, imageW - t.srcX);
                t.srcH = std::min(tileSize, imageH - t.srcY);
                if (t.srcW > 0 && t.srcH > 0)
                    out.push_back(t);
            }
        }
        return out;
    }
};
