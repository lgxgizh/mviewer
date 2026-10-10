#pragma once

#include "core/image/ImageBuffer.h"

#include <atomic>
#include <vector>

namespace mviewer::core
{

// Source rectangle in pixel coordinates. Empty when width or height is <= 0.
struct DisplayResampleRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

// One resample of `source` onto an exact target size. The caller picks the
// target in device pixels; this type does not know about widgets or DPR.
struct DisplayResampleRequest
{
    DisplayResampleRect source;
    int targetWidth = 0;
    int targetHeight = 0;
};

// Worker-owned scratch so repeated pane resamples do not reallocate.
// Not safe to share across threads.
struct DisplayResampleScratch
{
    std::vector<float> plane;
    std::vector<float> row;
    std::vector<float> accum;
    std::vector<float> samples;
    std::vector<float> prefix;
    std::vector<int> tapOffset;
    std::vector<int> tapIndex;
    std::vector<float> tapWeight;
    std::vector<int> tapOffsetY;
    std::vector<int> tapIndexY;
    std::vector<float> tapWeightY;
};

// Coverage of one displayed raster. Density is output pixels per source pixel
// on the finer axis (the smaller of the two scales).
struct DisplayRasterLevel
{
    int targetWidth = 0;
    int targetHeight = 0;
    int coveredSourceWidth = 0;
    int coveredSourceHeight = 0;
    int originX = 0;
    int originY = 0;
};

double displayRasterDensity(const DisplayRasterLevel &level) noexcept;

// True when `incoming` is a blurrier stand-in and must not replace `shown`
// while a sharper raster is still pending. Nothing shown is not a downgrade.
bool isBlurrierDisplayStandIn(const DisplayRasterLevel &shown,
                              const DisplayRasterLevel &incoming) noexcept;

// Index of a cached raster that may replace `shown`, or -1 to keep `shown`.
// Never returns a level blurrier than `shown`. Equal density prefers `desired`.
int selectDisplayStandIn(const DisplayRasterLevel *levels, int count,
                         const DisplayRasterLevel &shown, const DisplayRasterLevel &desired);

// High-quality resample of `request.source` to the exact target size.
// Identity (exact RGB) when the target matches the source rect. Large
// reductions are an area-average box down to about twice the target, then a
// separable prefiltered Catmull-Rom. Modest reductions use Catmull-Rom only.
// Output is RGB24. `scratch` may be null. When `cancel` becomes true the
// resample returns an empty image so a superseded scheduler job can exit
// without holding its worker until the full kernel finishes.
ImageData resampleDisplay(const ImageData &src, const DisplayResampleRequest &request,
                          DisplayResampleScratch *scratch = nullptr,
                          const std::atomic<bool> *cancel = nullptr);

} // namespace mviewer::core
