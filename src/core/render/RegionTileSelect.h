#pragma once

// Visible-region tile selection. Pure logic (no Qt, no decode) so viewport
// pans can prefer native/bounded region tiles and reuse TileCache keys.
//
// Tile identity stays (imageId, col, row, lod, renderScalePercent) — this
// helper only decides *which* tiles to ask for and *how* to fill them.

#include "core/render/Viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace mviewer::core
{

enum class TileFillPath : std::uint8_t
{
    NativeRegion,   // canNativeRegion: decodeRegion, no full-frame materialize
    BoundedCrop,    // bounded source pixels (clip/scan), not a full raster
    ReducedLod,     // nearly-full tile: one decodeLod instead of a full crop
    FullFrameScale, // scale an already-resident frame (caller owns the buffer)
    Skip            // no honest partial path and no resident frame
};

struct TileFillChoice
{
    TileFillPath path = TileFillPath::Skip;
    // True only when the choice would decode a full-resolution raster from
    // disk. Scaling a frame that is already in memory is not one of those.
    bool decodesFullRaster = false;
};

// `nearlyFull` — the requested rect covers (almost) the whole source.
// `coarse` — the source rect is at least 2x the tile, i.e. zoomed out.
//
// No resident frame (large-image LOD): native region, else bounded crop, so a
// zoomed-in pan never materializes the full raster. A resident frame is scaled
// in place when the tile is already near 1:1; coarse tiles still prefer a
// reduced/region decode so the full buffer is not walked per tile.
inline TileFillChoice chooseTileFill(bool hasPath, bool canNativeRegion, bool boundedRegion,
                                     bool nearlyFull, bool hasFullFrame, bool coarse)
{
    TileFillChoice choice;
    const bool wantSource = !hasFullFrame || coarse;
    if (wantSource && hasPath && nearlyFull)
    {
        choice.path = TileFillPath::ReducedLod;
        return choice;
    }
    if (wantSource && hasPath && canNativeRegion)
    {
        choice.path = TileFillPath::NativeRegion;
        return choice;
    }
    if (wantSource && hasPath && boundedRegion)
    {
        choice.path = TileFillPath::BoundedCrop;
        return choice;
    }
    if (hasFullFrame)
    {
        choice.path = TileFillPath::FullFrameScale;
        return choice;
    }
    choice.path = TileFillPath::Skip;
    return choice;
}

// Grow the viewport by `ring` source tiles so the tile scheduler pulls a
// one-tile halo. Those tiles use the same TileCache keys as a later pan.
// Screen size grows and the image origin shifts by the same margin, so the
// previously visible rect stays visible and LOD selection is unchanged.
inline Viewport inflateViewportForTileRing(const Viewport &viewport, int tileSize, int ring)
{
    Viewport out = viewport;
    if (ring <= 0 || tileSize <= 0 || !(viewport.scale > 0.0) || !std::isfinite(viewport.scale))
        return out;
    const double margin =
        static_cast<double>(tileSize) * static_cast<double>(ring) * viewport.scale;
    const double grownW = static_cast<double>(viewport.screenW) + margin * 2.0;
    const double grownH = static_cast<double>(viewport.screenH) + margin * 2.0;
    if (grownW > static_cast<double>(std::numeric_limits<int>::max()) ||
        grownH > static_cast<double>(std::numeric_limits<int>::max()))
        return out;
    out.screenW = static_cast<int>(std::ceil(grownW));
    out.screenH = static_cast<int>(std::ceil(grownH));
    out.offsetX -= margin;
    out.offsetY -= margin;
    return out;
}

struct PlannedTile
{
    int col = 0;
    int row = 0;
    bool visible = true;
};

// Visible columns/rows plus a capped ring of neighbors. `c0..c1` / `r0..r1`
// are inclusive tile indices. The ring is what a zoomed-in pan will need next;
// callers schedule both, and TileCache dedupes by key.
inline std::vector<PlannedTile> planViewportTiles(int c0, int r0, int c1, int r1, int cols,
                                                  int rows, int ring, int maxExtra)
{
    std::vector<PlannedTile> out;
    if (cols <= 0 || rows <= 0)
        return out;
    c0 = std::clamp(c0, 0, cols - 1);
    r0 = std::clamp(r0, 0, rows - 1);
    c1 = std::clamp(c1, 0, cols - 1);
    r1 = std::clamp(r1, 0, rows - 1);
    if (c1 < c0 || r1 < r0)
        return out;

    const int visCols = c1 - c0 + 1;
    const int visRows = r1 - r0 + 1;
    out.reserve(static_cast<size_t>(visCols) * static_cast<size_t>(visRows));
    for (int r = r0; r <= r1; ++r)
    {
        for (int c = c0; c <= c1; ++c)
            out.push_back(PlannedTile{c, r, true});
    }
    if (ring <= 0 || maxExtra <= 0)
        return out;

    const int ec0 = (std::max)(0, c0 - ring);
    const int er0 = (std::max)(0, r0 - ring);
    const int ec1 = (std::min)(cols - 1, c1 + ring);
    const int er1 = (std::min)(rows - 1, r1 + ring);
    int extra = 0;
    for (int r = er0; r <= er1 && extra < maxExtra; ++r)
    {
        for (int c = ec0; c <= ec1 && extra < maxExtra; ++c)
        {
            if (c >= c0 && c <= c1 && r >= r0 && r <= r1)
                continue;
            out.push_back(PlannedTile{c, r, false});
            ++extra;
        }
    }
    return out;
}

} // namespace mviewer::core
