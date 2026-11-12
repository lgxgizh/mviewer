#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvToRgb.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Scaled YUV -> RGB24 shared by renderYuvImage and readYuvFrameImage.
// On an axis where the output is smaller than the luma, each output pixel is a
// box average of the source luma samples that cover it. A 1:1 or upscaled axis
// keeps one nearest luma sample so full-size output stays sample-accurate.
// Chroma follows ChromaUpsample. Internal to the yuv module.

namespace mviewer::core::yuv::scaled_detail
{

// Output width (horizontal) or height for a longest-edge limit of maxEdge.
int outputEdge(int srcW, int srcH, int maxEdge, bool horizontal);

// RGB24 ImageData owning 'rgb' (empty when the size is not positive).
ImageData wrapRgb(int width, int height, std::vector<uint8_t> &&rgb);

// Renders unpacked planes at outW x outH. Luma is a box average on any axis that
// shrinks and nearest otherwise. Chroma is mixed per 'up'.
ImageData renderScaledPlanes(const YuvPlanes &planes, const Format &f, int outW, int outH,
                             ChromaUpsample up);

// Same pixels as unpacking 'data' (one frame laid out as 'layout', at least
// layout.frameBytes long) and calling renderScaledPlanes. Downscale decodes
// every luma sample in each output pixel's box; chroma reads the taps for 'up'.
ImageData renderScaledRaw(const uint8_t *data, const FrameLayout &layout, const Format &f, int outW,
                          int outH, ChromaUpsample up);

} // namespace mviewer::core::yuv::scaled_detail
