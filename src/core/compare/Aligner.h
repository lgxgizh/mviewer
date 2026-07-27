#pragma once

#include "core/image/ImageBuffer.h"

namespace mviewer
{

// Integer 2D translation that best registers `moving` onto `ref`.
struct AlignOffset
{
    int x = 0;
    int y = 0;
};

// Estimate / apply a 2D integer translation that best registers `moving` onto
// `ref`, so PSNR/SSIM/diff are computed on aligned frames instead of being
// dominated by mis-registration (common when comparing two renders of the same
// scene).
//
// Implementation: luminance SAD over a bounded search window, evaluated on a
// downscaled gray image for speed. Pure std (no Qt); operates on the domain
// ImageData type. Optional and off by default at the call site (ADR-M22.3).
class Aligner
{
  public:
    // maxShift: search radius in *downscaled* pixels (default 32).
    static AlignOffset estimate(const ImageData &ref, const ImageData &moving, int maxShift = 32);

    // Shift `src` by (dx,dy); uncovered pixels are filled with `fill`.
    static ImageData shift(const ImageData &src, int dx, int dy, uint8_t fill = 0);
};

} // namespace mviewer
