#pragma once

#include "ImageBuffer.h"

#include <algorithm>
#include <cmath>

// ─── M16.2: In-compare image adjustments (brightness / contrast / gamma / WB) ─
//
// Each function takes an src ImageData and returns an adjusted copy. The src is
// not modified. All functions handle RGB24 / RGBA32 / BGR24 / BGRA32 / Grayscale8.
//
// These are pure pixel-level operations; they belong in core/ (no Qt dependency)
// and are usable from the CompareWorkspace UI, batch, and test suites.

// Apply brightness offset to every pixel channel. offset is clamped to
// [-255, 255]; Grayscale receives the same offset on its single channel.
inline ImageData adjustBrightness(const ImageData &src, int offset)
{
    if (src.isNull())
        return src;

    ImageBuffer v = src.view();
    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    // For grayscale, all "channels" are the same single channel
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);
    const int adj = std::clamp(offset, -255, 255);

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            for (int c = 0; c < ch; ++c)
            {
                int val = static_cast<int>(s[x * cpp + c]) + adj;
                t[x * cpp + c] = static_cast<uint8_t>(std::clamp(val, 0, 255));
            }
            if (cpp == 4)
                t[x * cpp + 3] = s[x * cpp + 3]; // preserve alpha
        }
    }
    return dst;
}

// Apply contrast multiplier. 1.0 is identity, <1 reduces, >1 increases.
// Works by centering on 128: pixel = (pixel - 128) * factor + 128.
inline ImageData adjustContrast(const ImageData &src, float factor)
{
    if (src.isNull())
        return src;

    ImageBuffer v = src.view();
    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);
    const float f = std::max(factor, 0.0f);

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            for (int c = 0; c < ch; ++c)
            {
                float val = (static_cast<float>(s[x * cpp + c]) - 128.0f) * f + 128.0f;
                t[x * cpp + c] =
                    static_cast<uint8_t>(std::clamp(static_cast<int>(std::lroundf(val)), 0, 255));
            }
            if (cpp == 4)
                t[x * cpp + 3] = s[x * cpp + 3];
        }
    }
    return dst;
}

// Apply gamma correction (power law). 1.0 is identity; values > 1 darken,
// values < 1 lighten. Range clamped to [0.05, 8.0] for safety.
inline ImageData adjustGamma(const ImageData &src, float gamma)
{
    if (src.isNull())
        return src;

    const float g = std::clamp(gamma, 0.05f, 8.0f);
    if (std::abs(g - 1.0f) < 1e-6f)
        return src;

    ImageBuffer v = src.view();
    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);
    const float invGamma = 1.0f / g;

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            for (int c = 0; c < ch; ++c)
            {
                float norm = static_cast<float>(s[x * cpp + c]) / 255.0f;
                float corrected = std::pow(norm, invGamma);
                t[x * cpp + c] = static_cast<uint8_t>(std::lroundf(corrected * 255.0f));
            }
            if (cpp == 4)
                t[x * cpp + 3] = s[x * cpp + 3];
        }
    }
    return dst;
}

// Apply white-balance gain to R and B channels. 1.0 each is identity.
// Only affects color images (RGB/BGR). Grayscale is returned unchanged.
inline ImageData adjustWhiteBalance(const ImageData &src, float rGain, float bGain)
{
    if (src.isNull() || src.format == PixelFormat::Grayscale8)
        return src;

    const float r = std::max(rGain, 0.01f);
    const float b = std::max(bGain, 0.01f);
    if (std::abs(r - 1.0f) < 1e-6f && std::abs(b - 1.0f) < 1e-6f)
        return src;

    ImageBuffer v = src.view();
    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    // Determine channel offsets based on pixel format
    int rOff = 0, bOff = 2;
    if (src.format == PixelFormat::BGR24 || src.format == PixelFormat::BGRA32)
    {
        rOff = 2;
        bOff = 0;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            // Copy all channels first
            for (int c = 0; c < cpp; ++c)
                t[x * cpp + c] = s[x * cpp + c];

            // Apply R gain
            int rVal = static_cast<int>(std::lroundf(static_cast<float>(s[x * cpp + rOff]) * r));
            t[x * cpp + rOff] = static_cast<uint8_t>(std::clamp(rVal, 0, 255));

            // Apply B gain
            int bVal = static_cast<int>(std::lroundf(static_cast<float>(s[x * cpp + bOff]) * b));
            t[x * cpp + bOff] = static_cast<uint8_t>(std::clamp(bVal, 0, 255));
        }
    }
    return dst;
}
