#pragma once

#include <cstdint>
#include <vector>

namespace mviewer::core
{
// Pixel Inspector math — Qt-free, std-only so it is unit-testable without
// the widget layer. P0 #2 of M15 (Pixel Inspector Pro).
//
// The hovered pixel is reported in five color spaces an image-algorithm
// engineer actually reasons about, and a small neighborhood around it is
// summarized (Mean/Variance/StdDev/Min/Max) over an N×N kernel.

enum class ColorSpace
{
    RGB,
    HSV,
    Lab,
    YUV,
    YCbCr
};

// One triple in the selected color space. Channels are normalized to a
// human-readable range:
//   RGB   → 0..255, 0..255, 0..255
//   HSV   → H 0..360, S 0..100, V 0..100
//   Lab   → L 0..100, a -128..127, b -128..127
//   YUV   → Y 0..255, U -128..127, V -128..127  (BT.601)
//   YCbCr → Y 0..255, Cb 0..255, Cr 0..255      (BT.601, full range)
struct ColorTriple
{
    double c1 = 0, c2 = 0, c3 = 0;
};

// Convert an sRGB pixel (0..255 each) into the requested color space.
ColorTriple toColorSpace(uint8_t r, uint8_t g, uint8_t b, ColorSpace space);

struct NeighborhoodStats
{
    double mean = 0;     // mean of luminance over the kernel
    double variance = 0; // population variance of luminance
    double stdDev = 0;   // sqrt(variance)
    double min = 0;      // min luminance
    double max = 0;      // max luminance
    int count = 0;       // pixels actually sampled (clipped to image)
};

// Compute luminance statistics over an N×N neighborhood centered at (cx,cy)
// in an RGB24 buffer laid out row-major with the given stride (bytes/row).
// Out-of-bounds samples are skipped (the kernel is clipped to the image).
// `n` is the kernel half-width+1 (n=1 → 1×1, n=3 → 3×3, n=5 → 5×5, n=7 → 7×7).
NeighborhoodStats neighborhoodStats(const uint8_t *data, int stride, int width, int height, int cx,
                                    int cy, int n);

// Convert a ColorSpace enum to its short label (e.g. "RGB", "HSV").
const char *colorSpaceLabel(ColorSpace space);
} // namespace mviewer::core
