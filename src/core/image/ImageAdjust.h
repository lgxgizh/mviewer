#pragma once

#include "ImageBuffer.h"

#include <algorithm>
#include <array>
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

    const int adj = std::clamp(offset, -255, 255);
    if (adj == 0)
        return src;

    ImageBuffer v = src.view();
    if (v.width <= 0 || v.height <= 0)
        return src;

    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);

    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i)
    {
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(i + adj, 0, 255));
    }

    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * cpp) &&
                              (d.stride() == static_cast<ptrdiff_t>(d.width) * cpp);

    if (isContiguous && cpp == ch)
    {
        const size_t totalBytes =
            static_cast<size_t>(v.width) * static_cast<size_t>(v.height) * static_cast<size_t>(cpp);
        const uint8_t *s = v.data;
        uint8_t *t = d.data;
        for (size_t i = 0; i < totalBytes; ++i)
            t[i] = lut[s[i]];
        return dst;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            const size_t pxOff = static_cast<size_t>(x) * static_cast<size_t>(cpp);
            for (int c = 0; c < ch; ++c)
            {
                t[pxOff + static_cast<size_t>(c)] = lut[s[pxOff + static_cast<size_t>(c)]];
            }
            if (cpp == 4)
                t[pxOff + 3] = s[pxOff + 3]; // preserve alpha
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

    const float f = std::max(factor, 0.0f);
    if (std::abs(f - 1.0f) < 1e-6f)
        return src;

    ImageBuffer v = src.view();
    if (v.width <= 0 || v.height <= 0)
        return src;

    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);

    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i)
    {
        const float val = (static_cast<float>(i) - 128.0f) * f + 128.0f;
        lut[static_cast<size_t>(i)] =
            static_cast<uint8_t>(std::clamp(static_cast<int>(std::lroundf(val)), 0, 255));
    }

    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * cpp) &&
                              (d.stride() == static_cast<ptrdiff_t>(d.width) * cpp);

    if (isContiguous && cpp == ch)
    {
        const size_t totalBytes =
            static_cast<size_t>(v.width) * static_cast<size_t>(v.height) * static_cast<size_t>(cpp);
        const uint8_t *s = v.data;
        uint8_t *t = d.data;
        for (size_t i = 0; i < totalBytes; ++i)
            t[i] = lut[s[i]];
        return dst;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            const size_t pxOff = static_cast<size_t>(x) * static_cast<size_t>(cpp);
            for (int c = 0; c < ch; ++c)
            {
                t[pxOff + static_cast<size_t>(c)] = lut[s[pxOff + static_cast<size_t>(c)]];
            }
            if (cpp == 4)
                t[pxOff + 3] = s[pxOff + 3];
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
    if (v.width <= 0 || v.height <= 0)
        return src;

    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const int cpp = v.channelsPerPixel();
    const int ch = (src.format == PixelFormat::Grayscale8) ? 1 : std::min(cpp, 3);
    const float invGamma = 1.0f / g;

    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i)
    {
        const float norm = static_cast<float>(i) / 255.0f;
        const float corrected = std::pow(norm, invGamma);
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(std::lroundf(corrected * 255.0f)), 0, 255));
    }

    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * cpp) &&
                              (d.stride() == static_cast<ptrdiff_t>(d.width) * cpp);

    if (isContiguous && cpp == ch)
    {
        const size_t totalBytes =
            static_cast<size_t>(v.width) * static_cast<size_t>(v.height) * static_cast<size_t>(cpp);
        const uint8_t *s = v.data;
        uint8_t *t = d.data;
        for (size_t i = 0; i < totalBytes; ++i)
            t[i] = lut[s[i]];
        return dst;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            const size_t pxOff = static_cast<size_t>(x) * static_cast<size_t>(cpp);
            for (int c = 0; c < ch; ++c)
            {
                t[pxOff + static_cast<size_t>(c)] = lut[s[pxOff + static_cast<size_t>(c)]];
            }
            if (cpp == 4)
                t[pxOff + 3] = s[pxOff + 3];
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
    if (v.width <= 0 || v.height <= 0)
        return src;

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

    std::array<uint8_t, 256> lutR{};
    std::array<uint8_t, 256> lutB{};
    for (int i = 0; i < 256; ++i)
    {
        const int rVal = static_cast<int>(std::lroundf(static_cast<float>(i) * r));
        lutR[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(rVal, 0, 255));
        const int bVal = static_cast<int>(std::lroundf(static_cast<float>(i) * b));
        lutB[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(bVal, 0, 255));
    }

    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * cpp) &&
                              (d.stride() == static_cast<ptrdiff_t>(d.width) * cpp);

    if (isContiguous)
    {
        const size_t totalPixels = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);
        const uint8_t *s = v.data;
        uint8_t *t = d.data;
        for (size_t i = 0; i < totalPixels; ++i)
        {
            const size_t pxOff = i * static_cast<size_t>(cpp);
            for (int c = 0; c < cpp; ++c)
                t[pxOff + static_cast<size_t>(c)] = s[pxOff + static_cast<size_t>(c)];
            t[pxOff + static_cast<size_t>(rOff)] = lutR[s[pxOff + static_cast<size_t>(rOff)]];
            t[pxOff + static_cast<size_t>(bOff)] = lutB[s[pxOff + static_cast<size_t>(bOff)]];
        }
        return dst;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            const size_t pxOff = static_cast<size_t>(x) * static_cast<size_t>(cpp);
            // Copy all channels first
            for (int c = 0; c < cpp; ++c)
                t[pxOff + static_cast<size_t>(c)] = s[pxOff + static_cast<size_t>(c)];

            // Apply R and B gains from LUT
            t[pxOff + static_cast<size_t>(rOff)] = lutR[s[pxOff + static_cast<size_t>(rOff)]];
            t[pxOff + static_cast<size_t>(bOff)] = lutB[s[pxOff + static_cast<size_t>(bOff)]];
        }
    }
    return dst;
}
