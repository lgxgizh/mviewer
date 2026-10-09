#pragma once

// Tile seam geometry. Screen edges come from one shared Viewport transform.
// The uploaded tile carries a 1px (in source-texel steps) apron of real
// neighbouring pixels so GL_LINEAR / bilinear sampling at the content edge
// reads those neighbours instead of clamping to black or a repeated border.

#include "TileCache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mviewer::core
{

struct TileContentLayout
{
    int srcX = 0;
    int srcY = 0;
    int srcW = 0;
    int srcH = 0;
    int outW = 0;
    int outH = 0;
    int contentX = 0;
    int contentY = 0;
    int contentW = 0;
    int contentH = 0;
};

struct TileScreenPlacement
{
    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
    int contentX = 0;
    int contentY = 0;
    int contentW = 0;
    int contentH = 0;
    int texW = 0;
    int texH = 0;
    bool valid = false;
};

inline int apronOutputPixels(int sourcePixels, double pixPerSrc)
{
    if (sourcePixels <= 0)
        return 0;
    long rounded = std::lround(static_cast<double>(sourcePixels) * pixPerSrc);
    if (rounded < 1)
        rounded = 1;
    const long maxOut = std::numeric_limits<int>::max();
    if (rounded > maxOut)
        rounded = maxOut;
    return static_cast<int>(rounded);
}

inline void apronAxis(int imageExtent, int tileOrigin, int tileExtent, int canonical,
                      int &srcOrigin, int &srcExtent, int &outBefore, int &outAfter, int &outTotal)
{
    srcOrigin = tileOrigin;
    srcExtent = tileExtent;
    outBefore = 0;
    outAfter = 0;
    outTotal = canonical;
    if (imageExtent <= 0 || canonical <= 0 || tileExtent <= 0 || tileOrigin < 0)
        return;

    const int64_t spp64 = (static_cast<int64_t>(tileExtent) + canonical - 1) / canonical;
    int spp = 1;
    if (spp64 > tileExtent)
        spp = tileExtent;
    else if (spp64 > 1)
        spp = static_cast<int>(spp64);

    const int before = (std::min)(spp, tileOrigin);
    const int after = (std::min)(spp, (std::max)(0, imageExtent - (tileOrigin + tileExtent)));
    const double pixPerSrc = static_cast<double>(canonical) / static_cast<double>(tileExtent);
    outBefore = apronOutputPixels(before, pixPerSrc);
    outAfter = apronOutputPixels(after, pixPerSrc);
    const int64_t maxOut = std::numeric_limits<int>::max();
    const int64_t total = static_cast<int64_t>(canonical) + outBefore + outAfter;
    outTotal = static_cast<int>((std::min)(total, maxOut));
    srcOrigin = tileOrigin - before;
    const int64_t extent = static_cast<int64_t>(tileExtent) + before + after;
    srcExtent = static_cast<int>((std::min)(extent, maxOut));
}

inline TileContentLayout tileContentLayout(int imageW, int imageH, int tileX, int tileY, int tileW,
                                           int tileH, int canonW, int canonH)
{
    TileContentLayout layout;
    if (tileW <= 0 || tileH <= 0 || canonW <= 0 || canonH <= 0)
        return layout;
    layout.srcX = tileX;
    layout.srcY = tileY;
    layout.srcW = tileW;
    layout.srcH = tileH;
    layout.outW = canonW;
    layout.outH = canonH;
    layout.contentW = canonW;
    layout.contentH = canonH;
    if (imageW <= 0 || imageH <= 0)
        return layout;

    int srcX = tileX;
    int srcW = tileW;
    int outBeforeX = 0;
    int outAfterX = 0;
    int outW = canonW;
    apronAxis(imageW, tileX, tileW, canonW, srcX, srcW, outBeforeX, outAfterX, outW);
    int srcY = tileY;
    int srcH = tileH;
    int outBeforeY = 0;
    int outAfterY = 0;
    int outH = canonH;
    apronAxis(imageH, tileY, tileH, canonH, srcY, srcH, outBeforeY, outAfterY, outH);
    layout.srcX = srcX;
    layout.srcY = srcY;
    layout.srcW = srcW;
    layout.srcH = srcH;
    layout.outW = outW;
    layout.outH = outH;
    layout.contentX = outBeforeX;
    layout.contentY = outBeforeY;
    layout.contentW = canonW;
    layout.contentH = canonH;
    return layout;
}

// Screen rect of the logical tile (not the apron), plus the texels to sample.
// A decoded image whose size matches the apron layout samples the inner
// content. Any other size is treated as the whole texture (legacy payloads).
inline TileScreenPlacement placeTile(const Viewport &viewport, int imageW, int imageH, int tileSize,
                                     int col, int row, int lod, int renderScalePercent,
                                     int decodedW, int decodedH)
{
    TileScreenPlacement placed;
    const int lodSize = TileCache::lodTileSize(tileSize, lod);
    if (lodSize <= 0 || imageW <= 0 || imageH <= 0 || decodedW <= 0 || decodedH <= 0)
        return placed;
    const int64_t originX = static_cast<int64_t>(col) * lodSize;
    const int64_t originY = static_cast<int64_t>(row) * lodSize;
    if (originX < 0 || originY < 0 || originX >= imageW || originY >= imageH)
        return placed;
    const int tileX = static_cast<int>(originX);
    const int tileY = static_cast<int>(originY);
    const int tileW = static_cast<int>(
        (std::min)(static_cast<int64_t>(lodSize), static_cast<int64_t>(imageW) - originX));
    const int tileH = static_cast<int>(
        (std::min)(static_cast<int64_t>(lodSize), static_cast<int64_t>(imageH) - originY));
    if (tileW <= 0 || tileH <= 0)
        return placed;
    const int canonW = TileCache::canonicalTilePixels(tileW, tileSize, lod, renderScalePercent);
    const int canonH = TileCache::canonicalTilePixels(tileH, tileSize, lod, renderScalePercent);
    const TileContentLayout layout =
        tileContentLayout(imageW, imageH, tileX, tileY, tileW, tileH, canonW, canonH);
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    viewport.imageRectToScreen(tileX, tileY, tileW, tileH, sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return placed;
    placed.screenX = sx;
    placed.screenY = sy;
    placed.screenW = sw;
    placed.screenH = sh;
    placed.texW = decodedW;
    placed.texH = decodedH;
    const bool apron = decodedW == layout.outW && decodedH == layout.outH && layout.contentW > 0 &&
                       layout.contentH > 0;
    if (apron)
    {
        placed.contentX = layout.contentX;
        placed.contentY = layout.contentY;
        placed.contentW = layout.contentW;
        placed.contentH = layout.contentH;
    }
    else
    {
        placed.contentX = 0;
        placed.contentY = 0;
        placed.contentW = decodedW;
        placed.contentH = decodedH;
    }
    if (placed.contentW <= 0 || placed.contentH <= 0)
        return placed;
    placed.valid = true;
    return placed;
}

} // namespace mviewer::core
