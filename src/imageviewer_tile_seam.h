#pragma once

// Viewer-only tile decode. Expands the canonical tile by a neighbour apron
// before the existing reduced-decode / full-frame fallback. Canonical sizes
// passed into TileCache stay unchanged; only the pixels stored for a key grow.

#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/render/TileSeam.h"
#include "core/render/TileSourceDecode.h"

#include <string>

inline ImageData decodeViewerTile(const std::string &path, const ImageData &fullFrame, int imageW,
                                  int imageH, int tileX, int tileY, int tileW, int tileH,
                                  int canonW, int canonH,
                                  const mviewer::domain::ImageMetadata &metadata,
                                  const mviewer::core::DisplayColorContext &target)
{
    if (tileW <= 0 || tileH <= 0 || canonW <= 0 || canonH <= 0)
        return {};
    const mviewer::core::TileContentLayout layout = mviewer::core::tileContentLayout(
        imageW, imageH, tileX, tileY, tileW, tileH, canonW, canonH);
    if (layout.srcW <= 0 || layout.srcH <= 0 || layout.outW <= 0 || layout.outH <= 0)
        return {};
    ImageData raw =
        mviewer::core::decodeTilePreferReduced(path, fullFrame, layout.srcX, layout.srcY,
                                               layout.srcW, layout.srcH, layout.outW, layout.outH);
    if (raw.isNull() && !fullFrame.isNull())
    {
        raw = RenderEngine::scaleRegionStatic(
            fullFrame, RenderRect{layout.srcX, layout.srcY, layout.srcW, layout.srcH},
            RenderSize{layout.outW, layout.outH}, RenderInterp::Bilinear);
    }
    if (raw.isNull())
        return {};
    return mvcore::toDisplayImageData(raw, metadata, target);
}
