#pragma once

#include "core/image/ImageBuffer.h"

// Format-aware pixel reads shared by the built-in analyzers.
//
// Luma is the one canonical Rec.601 form, lumaRec601_8() in ImageBuffer.h
// (rounded 16.16 fixed point; also behind luminance() on the histogram path).
// Noise, sharpness, entropy, dead-pixel and MTF intentionally use the plain
// channel mean (R+G+B)/3 instead — a different quantity, so it is named
// rgbMean*, not luma. Only the unpack is shared.

struct Rgb8
{
    int r = 0;
    int g = 0;
    int b = 0;
};

inline Rgb8 unpackRgb8(const uint8_t *p, PixelFormat format) noexcept
{
    if (p == nullptr)
        return {};
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    getPixelRGB(p, format, r, g, b);
    return Rgb8{static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)};
}

inline int lumaRec601(Rgb8 rgb) noexcept
{
    return lumaRec601_8(rgb.r, rgb.g, rgb.b);
}

// Channel mean, truncated (not luma).
inline int rgbMean(Rgb8 rgb) noexcept
{
    return (rgb.r + rgb.g + rgb.b) / 3;
}

// Channel mean as a double (not luma).
inline double rgbMeanF(Rgb8 rgb) noexcept
{
    return (static_cast<double>(rgb.r) + static_cast<double>(rgb.g) + static_cast<double>(rgb.b)) /
           3.0;
}
