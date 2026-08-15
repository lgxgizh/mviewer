#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/image/ImageBuffer.h"

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
    YCbCr,
    XYZ, // CIE XYZ (D65), each channel in 0..~1.2
    HEX  // #RRGGBB string form of RGB (display-only; see toHex)
};

// One triple in the selected color space. Channels are normalized to a
// human-readable range:
//   RGB   → 0..255, 0..255, 0..255
//   HSV   → H 0..360, S 0..100, V 0..100
//   Lab   → L 0..100, a -128..127, b -128..127
//   YUV   → Y 0..255, U -128..127, V -128..127  (BT.601)
//   YCbCr → Y 0..255, Cb 0..255, Cr 0..255      (BT.601, full range)
//   XYZ   → X 0..~0.95, Y 0..1, Z 0..~1.08      (D65)
//   HEX   → same as RGB (display formatted via toHex)
struct ColorTriple
{
    double c1 = 0, c2 = 0, c3 = 0;
};

// Convert an sRGB pixel (0..255 each) into the requested color space.
ColorTriple toColorSpace(uint8_t r, uint8_t g, uint8_t b, ColorSpace space);

// Convert a high-bit-depth pixel (0..maxVal each channel) into the requested
// color space. Used when a true 16-bit source is available so the Pixel
// Inspector readout is not quantized to 8-bit. RGB/HEX return the integer
// sample (RGB scaled to 8-bit for HEX display); chromaticity spaces are
// computed from the normalized (0..1) sample and share the 8-bit output ranges.
ColorTriple toColorSpace(uint16_t r, uint16_t g, uint16_t b, uint16_t maxVal, ColorSpace space);

struct NeighborhoodStats
{
    double mean = 0;     // mean of luminance over the kernel
    double variance = 0; // population variance of luminance
    double stdDev = 0;   // sqrt(variance)
    double min = 0;      // min luminance
    double max = 0;      // max luminance
    int count = 0;       // pixels actually sampled (clipped to image)
    double rMean = 0;    // mean of R channel over the kernel
    double gMean = 0;    // mean of G channel over the kernel
    double bMean = 0;    // mean of B channel over the kernel
};

// Coordinate-space adjustment state used by source-backed analysis. The
// adjusted coordinate is in the crop-then-rotate output space, while pixels
// are sampled lazily from the original ImageData. This keeps Inspector and
// neighborhood analysis exact without materializing a full-resolution QImage.
struct AnalysisAdjustment
{
    int brightness = 0;
    double contrast = 1.0;
    double gamma = 1.0;
    double redGain = 1.0;
    double blueGain = 1.0;
    int rotation = 0;
    bool hasCrop = false;
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;
};

struct AnalysisPixel
{
    int r = 0;
    int g = 0;
    int b = 0;
    bool valid = false;
};

// Sample one exact pixel from the original source in adjusted-pane
// coordinates. The operation order matches ImageAdjust/applyAdjusts:
// brightness → contrast → gamma → white balance → crop → rotation in the
// coordinate mapping (point operations are evaluated at the inverse-mapped
// source pixel). No display QImage or full-resolution adjusted copy is made.
AnalysisPixel sampleAnalysisPixel(const ImageData &source, const AnalysisAdjustment &adjustment,
                                  int adjustedX, int adjustedY);

// Compute an exact N×N neighborhood over source-backed adjusted pixels.
// Out-of-bounds samples are clipped; n=1,3,5,7 are the supported Inspector
// kernels used by the UI.
NeighborhoodStats neighborhoodStats(const ImageData &source,
                                    const AnalysisAdjustment &adjustment, int adjustedX,
                                    int adjustedY, int n);

// Compute luminance statistics over an N×N neighborhood centered at (cx,cy)
// in an RGB24 buffer laid out row-major with the given stride (bytes/row).
// Out-of-bounds samples are skipped (the kernel is clipped to the image).
// `n` is the kernel half-width+1 (n=1 → 1×1, n=3 → 3×3, n=5 → 5×5, n=7 → 7×7).
NeighborhoodStats neighborhoodStats(const uint8_t *data, int stride, int width, int height, int cx,
                                    int cy, int n);

// Convert a ColorSpace enum to its short label (e.g. "RGB", "HSV").
const char *colorSpaceLabel(ColorSpace space);

// Format an sRGB pixel as a "#RRGGBB" string.
std::string toHex(uint8_t r, uint8_t g, uint8_t b);
} // namespace mviewer::core
