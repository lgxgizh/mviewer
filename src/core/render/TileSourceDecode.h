#pragma once

#include "core/image/DisplayMip.h"
#include "core/image/ExifOrientation.h"
#include "core/image/ImageBuffer.h"
#include "core/image/SourceImage.h"
#include "core/render/RenderEngine.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace mviewer::core
{

// Prefer reduced rasters when filling coarse TileCache tiles:
//   1) CacheManager getBestMip (via tryBestMip)
//   2) SourceImage::decodeLod / decodeRegion when path is available
//   3) empty → caller scales from the already-decoded full frame
//
// `srcX/Y/W/H` are in the same space as `fullFrame` (displayed / oriented).
inline ImageData decodeTilePreferReduced(const std::string &path, const ImageData &fullFrame,
                                         int srcX, int srcY, int srcW, int srcH, int targetW,
                                         int targetH)
{
    if (srcW <= 0 || srcH <= 0 || targetW <= 0 || targetH <= 0)
        return ImageData{};

    const int fullW = !fullFrame.isNull() ? fullFrame.width : 0;
    const int fullH = !fullFrame.isNull() ? fullFrame.height : 0;

    auto scaleFrom = [&](const ImageData &src, int baseW, int baseH) -> ImageData
    {
        if (src.isNull() || baseW <= 0 || baseH <= 0)
            return ImageData{};
        const double sx = static_cast<double>(src.width) / static_cast<double>(baseW);
        const double sy = static_cast<double>(src.height) / static_cast<double>(baseH);
        const int rx = std::max(0, static_cast<int>(std::floor(srcX * sx)));
        const int ry = std::max(0, static_cast<int>(std::floor(srcY * sy)));
        int rw = std::max(1, static_cast<int>(std::ceil(srcW * sx)));
        int rh = std::max(1, static_cast<int>(std::ceil(srcH * sy)));
        if (rx >= src.width || ry >= src.height)
            return ImageData{};
        rw = std::min(rw, src.width - rx);
        rh = std::min(rh, src.height - ry);
        if (rw <= 0 || rh <= 0)
            return ImageData{};
        return RenderEngine::scaleRegionStatic(
            src, RenderRect{rx, ry, rw, rh}, RenderSize{targetW, targetH}, RenderInterp::Bilinear);
    };

    int baseEdge = std::max(fullW, fullH);
    if (baseEdge <= 0)
        baseEdge = std::max(srcW, srcH);
    const double dens =
        static_cast<double>(std::max(targetW, targetH)) / static_cast<double>(std::max(srcW, srcH));
    const int wantEdge =
        std::max(1, static_cast<int>(std::ceil(static_cast<double>(baseEdge) * dens)));

    // 1) In-memory mip chain (cheap).
    if (!path.empty() && wantEdge > 0)
    {
        ImageData mip = tryBestMip(path, wantEdge);
        if (!mip.isNull())
        {
            const int bw = fullW > 0 ? fullW : mip.width;
            const int bh = fullH > 0 ? fullH : mip.height;
            ImageData fromMip = scaleFrom(mip, bw, bh);
            if (!fromMip.isNull())
                return fromMip;
        }
    }

    // 2) Coarse tile: prefer decodeLod / decodeRegion over scaling a huge frame.
    const bool coarse = std::max(srcW, srcH) >= 2 * std::max(targetW, targetH);
    if (!coarse || path.empty())
        return ImageData{};

    auto source = SourceImage::open(path);
    if (!source || !source->isValid())
        return ImageData{};

    const int srcFullW = source->metadata().width;
    const int srcFullH = source->metadata().height;
    const bool nearlyFull = srcFullW > 0 && srcFullH > 0 && srcX <= 1 && srcY <= 1 &&
                            srcW >= srcFullW - 2 && srcH >= srcFullH - 2;
    if (nearlyFull)
    {
        auto lod = source->decodeLod(wantEdge);
        if (lod.ok && !lod.pixels.isNull())
        {
            ImageData fromLod = scaleFrom(lod.pixels, srcFullW > 0 ? srcFullW : lod.pixels.width,
                                          srcFullH > 0 ? srcFullH : lod.pixels.height);
            if (!fromLod.isNull())
                return fromLod;
        }
        return ImageData{};
    }

    const SourceRect raw = orientedRectToRaw({srcX, srcY, srcW, srcH}, source->rawWidth(),
                                             source->rawHeight(), source->orientation());
    auto region = source->decodeRegion(raw, targetW, targetH);
    if (!region.ok || region.pixels.isNull())
        return ImageData{};
    if (region.pixels.width == targetW && region.pixels.height == targetH)
        return region.pixels;
    return RenderEngine::scaleBoundedStatic(region.pixels, RenderSize{targetW, targetH});
}

} // namespace mviewer::core
