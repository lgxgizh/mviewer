#pragma once

#include "core/image/ImageBuffer.h"
#include "core/render/RenderEngine.h"

#include <algorithm>
#include <cmath>

namespace mviewer::ui
{

// Max edge for low-precision live diff while the user is interacting.
// Full-precision diff still runs on settle (flushDeferredCompareAnalysis).
constexpr int kLiveDiffMaxEdge = 384;

// Downscale (or pass-through) for cheap live DifferenceEngine input.
inline ImageData downscaleForLiveDiff(const ImageData &src, int maxEdge = kLiveDiffMaxEdge)
{
    if (src.isNull() || maxEdge <= 0)
        return ImageData{};
    if (src.width <= maxEdge && src.height <= maxEdge)
        return src;
    const int srcEdge = std::max(src.width, src.height);
    const double ratio = static_cast<double>(maxEdge) / static_cast<double>(srcEdge);
    const int tw = std::max(1, static_cast<int>(std::lround(src.width * ratio)));
    const int th = std::max(1, static_cast<int>(std::lround(src.height * ratio)));
    return RenderEngine::scaleBoundedStatic(src, RenderSize{tw, th});
}

} // namespace mviewer::ui
