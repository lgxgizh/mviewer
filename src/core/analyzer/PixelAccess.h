#pragma once

#include "core/image/ImageBuffer.h"

// Format-aware pixel reads shared by the built-in analyzers.
//
// Rec.601 here is the integer form those analyzers already agree on:
// (19595*R + 38470*G + 7471*B) >> 16. ImageBuffer::luminance() is a
// different floating-point truncation and stays on the histogram path.
// Noise, sharpness, entropy and dead-pixel intentionally use the channel
// average instead of Rec.601; only the unpack is shared.

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
    return (19595 * rgb.r + 38470 * rgb.g + 7471 * rgb.b) >> 16;
}

inline int lumaAverage(Rgb8 rgb) noexcept
{
    return (rgb.r + rgb.g + rgb.b) / 3;
}

inline double lumaAverageF(Rgb8 rgb) noexcept
{
    return (static_cast<double>(rgb.r) + static_cast<double>(rgb.g) + static_cast<double>(rgb.b)) /
           3.0;
}
