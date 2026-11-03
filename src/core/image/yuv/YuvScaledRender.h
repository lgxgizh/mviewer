#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvToRgb.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Scaled-down (nearest) YUV -> RGB24 rendering shared by renderYuvImage and
// readYuvFrameImage. Internal to the yuv module.

namespace mviewer::core::yuv::scaled_detail
{

// Output width (horizontal) or height for a longest-edge limit of maxEdge.
int outputEdge(int srcW, int srcH, int maxEdge, bool horizontal);

// RGB24 ImageData owning 'rgb' (empty when the size is not positive).
ImageData wrapRgb(int width, int height, std::vector<uint8_t> &&rgb);

// Nearest-samples unpacked planes to outW x outH (chroma mixed per 'up').
ImageData renderScaledPlanes(const YuvPlanes &planes, const Format &f, int outW, int outH,
                             ChromaUpsample up);

// Same pixels as unpacking 'data' (one frame laid out as 'layout', at least
// layout.frameBytes long) and calling renderScaledPlanes, but only the sampled
// samples are decoded.
ImageData renderScaledRaw(const uint8_t *data, const FrameLayout &layout, const Format &f, int outW,
                          int outH, ChromaUpsample up);

} // namespace mviewer::core::yuv::scaled_detail
