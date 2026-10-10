#pragma once

#include "core/image/ImageBuffer.h"

#include <atomic>
#include <cstdint>
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

enum class DisplayResampleQuality : std::uint8_t
{
    Sharp,
    Preview
};

// One resample of `source` onto an exact target size. The caller picks the
// target in device pixels; this type does not know about widgets or DPR.
// Preview is a fast bilinear stand-in; Sharp is the separable kernel.
struct DisplayResampleRequest
{
    DisplayResampleRect source;
    int targetWidth = 0;
    int targetHeight = 0;
    DisplayResampleQuality quality = DisplayResampleQuality::Sharp;
};

// Worker-owned scratch so repeated pane resamples do not reallocate.
// Not safe to share across threads. Tap tables are read-only once built.
struct DisplayResampleScratch
{
    std::vector<int> originX;
    std::vector<int> countX;
    std::vector<int16_t> weightX;
    std::vector<int> originY;
    std::vector<int> countY;
    std::vector<int16_t> weightY;
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

// Drops cached 2x box levels. In-flight resamples keep the buffers they
// already took. Compare session teardown calls this with the pane caches.
void releaseDisplayResampleCache() noexcept;

// 0 auto, 1 scalar, 2 SSE4.1, 3 AVX2. Test hook; production leaves this at 0.
void setDisplayResampleIsaForTest(int isa) noexcept;

// High-quality resample of `request.source` to the exact target size.
// Identity (exact RGB) when the target matches the source rect. Reductions
// of about 2x or more first take exact 2x box steps until the remainder is
// in [1, 2), then a separable fixed-point Catmull-Rom. Preview uses bilinear
// for that last step. Output is RGB24. `scratch` may be null. When `cancel`
// becomes true the resample returns an empty image.
ImageData resampleDisplay(const ImageData &src, const DisplayResampleRequest &request,
                          DisplayResampleScratch *scratch = nullptr,
                          const std::atomic<bool> *cancel = nullptr);

} // namespace mviewer::core
