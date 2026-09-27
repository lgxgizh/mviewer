#pragma once

#include "core/image/DisplayMip.h"
#include "core/image/ExifOrientation.h"
#include "core/image/ImageBuffer.h"
#include "core/image/SourceImage.h"
#include "core/render/RegionTileSelect.h"
#include "core/render/RenderEngine.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace mviewer::core
{

// Fill one TileCache tile without a full-frame materialize when the source
// can do better:
//   1) CacheManager getBestMip (via tryBestMip)
//   2) Native region (canNativeRegion) or bounded crop, including zoomed-in
//      tiles — not only coarse LODs
//   3) decodeLod when the tile covers almost the whole image
//   4) empty → caller scales an already-resident full frame, or skips
//
// `srcX/Y/W/H` are in the same space as `fullFrame` (displayed / oriented).
inline ImageData decodeTileFromSource(SourceImage &source, const ImageData &fullFrame, int srcX,
                                      int srcY, int srcW, int srcH, int targetW, int targetH)
{
    const int srcFullW = source.metadata().width;
    const int srcFullH = source.metadata().height;
    const bool nearlyFull = srcFullW > 0 && srcFullH > 0 && srcX <= 1 && srcY <= 1 &&
                            srcW >= srcFullW - 2 && srcH >= srcFullH - 2;
    const bool coarse = std::max(srcW, srcH) >= 2 * std::max(targetW, targetH);
    const auto regionPath = source.regionDecodePath();
    const bool native = regionPath == SourceDecodePath::NativeRegion;
    const bool bounded = regionPath == SourceDecodePath::BoundedRasterRegion;
    const TileFillChoice choice =
        chooseTileFill(true, native, bounded, nearlyFull, !fullFrame.isNull(), coarse);

    auto scaleToTarget = [&](ImageData pixels) -> ImageData
    {
        if (pixels.isNull())
            return {};
        if (pixels.width == targetW && pixels.height == targetH)
            return pixels;
        return RenderEngine::scaleBoundedStatic(pixels, RenderSize{targetW, targetH});
    };

    if (choice.path == TileFillPath::ReducedLod)
    {
        const int fullW = !fullFrame.isNull() ? fullFrame.width : srcFullW;
        const int fullH = !fullFrame.isNull() ? fullFrame.height : srcFullH;
        int baseEdge = std::max(fullW, fullH);
        if (baseEdge <= 0)
            baseEdge = std::max(srcW, srcH);
        const double dens = static_cast<double>(std::max(targetW, targetH)) /
                            static_cast<double>(std::max(1, std::max(srcW, srcH)));
        const int wantEdge =
            std::max(1, static_cast<int>(std::ceil(static_cast<double>(baseEdge) * dens)));
        auto lod = source.decodeLod(wantEdge);
        if (!lod.ok || lod.pixels.isNull())
            return {};
        const int bw = srcFullW > 0 ? srcFullW : lod.pixels.width;
        const int bh = srcFullH > 0 ? srcFullH : lod.pixels.height;
        if (bw <= 0 || bh <= 0)
            return {};
        const double sx = static_cast<double>(lod.pixels.width) / static_cast<double>(bw);
        const double sy = static_cast<double>(lod.pixels.height) / static_cast<double>(bh);
        const int rx = std::max(0, static_cast<int>(std::floor(srcX * sx)));
        const int ry = std::max(0, static_cast<int>(std::floor(srcY * sy)));
        int rw = std::max(1, static_cast<int>(std::ceil(srcW * sx)));
        int rh = std::max(1, static_cast<int>(std::ceil(srcH * sy)));
        if (rx >= lod.pixels.width || ry >= lod.pixels.height)
            return {};
        rw = std::min(rw, lod.pixels.width - rx);
        rh = std::min(rh, lod.pixels.height - ry);
        return RenderEngine::scaleRegionStatic(lod.pixels, RenderRect{rx, ry, rw, rh},
                                               RenderSize{targetW, targetH},
                                               RenderInterp::Bilinear);
    }

    if (choice.path != TileFillPath::NativeRegion && choice.path != TileFillPath::BoundedCrop)
        return {};

    const SourceRect raw = orientedRectToRaw({srcX, srcY, srcW, srcH}, source.rawWidth(),
                                             source.rawHeight(), source.orientation());
    auto region = source.decodeRegion(raw, targetW, targetH);
    if (!region.ok || region.pixels.isNull())
        return {};
    // NativeRegion and BoundedRasterRegion are the only results that did not
    // materialize a full raster. FullDecodeCrop is rejected so a zoomed-in pan
    // cannot silently decode the whole file per tile.
    if (region.decodePath != SourceDecodePath::NativeRegion &&
        region.decodePath != SourceDecodePath::BoundedRasterRegion)
        return {};
    return scaleToTarget(std::move(region.pixels));
}

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

    // 2) Zoomed-in tiles with no resident frame, and coarse tiles: native
    //    region, else bounded crop, else decodeLod. A resident 1:1 frame is
    //    scaled by the caller — don't open the file per tile.
    const bool coarse = std::max(srcW, srcH) >= 2 * std::max(targetW, targetH);
    if (path.empty() || (!fullFrame.isNull() && !coarse))
        return ImageData{};
    auto source = SourceImage::open(path);
    if (!source || !source->isValid())
        return ImageData{};
    return decodeTileFromSource(*source, fullFrame, srcX, srcY, srcW, srcH, targetW, targetH);
}

} // namespace mviewer::core
