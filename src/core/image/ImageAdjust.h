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

    const ConstImageView v = src.constView();
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

    const ConstImageView v = src.constView();
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

    const ConstImageView v = src.constView();
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

    const ConstImageView v = src.constView();
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

// Build 3x256 fused LUT for brightness, contrast, gamma, and white balance.
inline void buildCombinedLuts(int adj, float f, float g, float r, float b, bool isGray,
                              std::array<uint8_t, 256> &lutR, std::array<uint8_t, 256> &lutG,
                              std::array<uint8_t, 256> &lutB)
{
    const bool hasBrightness = (adj != 0);
    const bool hasContrast = (std::abs(f - 1.0f) >= 1e-6f);
    const bool hasGamma = (std::abs(g - 1.0f) >= 1e-6f);
    const bool hasWb = (std::abs(r - 1.0f) >= 1e-6f || std::abs(b - 1.0f) >= 1e-6f);
    const float clampedGamma = std::clamp(g, 0.05f, 8.0f);
    const float invGamma = 1.0f / clampedGamma;

    for (int i = 0; i < 256; ++i)
    {
        int val = i;
        if (hasBrightness)
            val = std::clamp(val + std::clamp(adj, -255, 255), 0, 255);
        if (hasContrast)
        {
            const float v = (static_cast<float>(val) - 128.0f) * std::max(f, 0.0f) + 128.0f;
            val = std::clamp(static_cast<int>(std::lroundf(v)), 0, 255);
        }
        if (hasGamma)
        {
            const float norm = static_cast<float>(val) / 255.0f;
            val = std::clamp(static_cast<int>(std::lroundf(std::pow(norm, invGamma) * 255.0f)), 0,
                             255);
        }

        if (isGray)
        {
            lutR[static_cast<size_t>(i)] = static_cast<uint8_t>(val);
            lutG[static_cast<size_t>(i)] = static_cast<uint8_t>(val);
            lutB[static_cast<size_t>(i)] = static_cast<uint8_t>(val);
        }
        else
        {
            int rVal = val;
            int bVal = val;
            if (hasWb)
            {
                if (std::abs(r - 1.0f) >= 1e-6f)
                    rVal = std::clamp(static_cast<int>(std::lroundf(static_cast<float>(val) *
                                                                    std::max(r, 0.01f))),
                                      0, 255);
                if (std::abs(b - 1.0f) >= 1e-6f)
                    bVal = std::clamp(static_cast<int>(std::lroundf(static_cast<float>(val) *
                                                                    std::max(b, 0.01f))),
                                      0, 255);
            }
            lutR[static_cast<size_t>(i)] = static_cast<uint8_t>(rVal);
            lutG[static_cast<size_t>(i)] = static_cast<uint8_t>(val);
            lutB[static_cast<size_t>(i)] = static_cast<uint8_t>(bVal);
        }
    }
}

// Apply the precomputed fused LUTs. Source is read-only; destination is written.
inline void applyCombinedLuts(const ConstImageView &v, ImageBuffer &d,
                              const std::array<uint8_t, 256> &lutR,
                              const std::array<uint8_t, 256> &lutG,
                              const std::array<uint8_t, 256> &lutB, PixelFormat fmt, int cpp)
{
    const bool isGray = (fmt == PixelFormat::Grayscale8);
    int rOff = 0, gOff = 1, bOff = 2;
    if (fmt == PixelFormat::BGR24 || fmt == PixelFormat::BGRA32)
    {
        rOff = 2;
        bOff = 0;
    }

    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * cpp) &&
                              (d.stride() == static_cast<ptrdiff_t>(d.width) * cpp);

    if (isGray)
    {
        if (isContiguous)
        {
            const size_t total = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);
            for (size_t i = 0; i < total; ++i)
                d.data[i] = lutR[v.data[i]];
            return;
        }
        for (int y = 0; y < v.height; ++y)
        {
            const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
            uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
            for (int x = 0; x < v.width; ++x)
                t[x] = lutR[s[x]];
        }
        return;
    }

    if (isContiguous)
    {
        const size_t total = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);
        for (size_t i = 0; i < total; ++i)
        {
            const size_t off = i * static_cast<size_t>(cpp);
            d.data[off + static_cast<size_t>(rOff)] = lutR[v.data[off + static_cast<size_t>(rOff)]];
            d.data[off + static_cast<size_t>(gOff)] = lutG[v.data[off + static_cast<size_t>(gOff)]];
            d.data[off + static_cast<size_t>(bOff)] = lutB[v.data[off + static_cast<size_t>(bOff)]];
            if (cpp == 4)
                d.data[off + 3] = v.data[off + 3];
        }
        return;
    }

    for (int y = 0; y < v.height; ++y)
    {
        const uint8_t *s = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *t = d.data + static_cast<size_t>(y) * d.stride();
        for (int x = 0; x < v.width; ++x)
        {
            const size_t off = static_cast<size_t>(x) * static_cast<size_t>(cpp);
            t[off + static_cast<size_t>(rOff)] = lutR[s[off + static_cast<size_t>(rOff)]];
            t[off + static_cast<size_t>(gOff)] = lutG[s[off + static_cast<size_t>(gOff)]];
            t[off + static_cast<size_t>(bOff)] = lutB[s[off + static_cast<size_t>(bOff)]];
            if (cpp == 4)
                t[off + 3] = s[off + 3];
        }
    }
}

// Fused 1D LUT applying brightness, contrast, gamma, and white balance in a
// single pass with a single allocation, eliminating redundant intermediate
// full-image buffers and cascade quantization artifacts.
inline ImageData adjustCombinedLut(const ImageData &src, int brightness, float contrast,
                                   float gamma, float rGain, float bGain)
{
    if (src.isNull())
        return src;

    const int adj = std::clamp(brightness, -255, 255);
    const float f = std::max(contrast, 0.0f);
    const float g = std::clamp(gamma, 0.05f, 8.0f);
    const float r = std::max(rGain, 0.01f);
    const float b = std::max(bGain, 0.01f);

    if (adj == 0 && std::abs(f - 1.0f) < 1e-6f && std::abs(g - 1.0f) < 1e-6f &&
        std::abs(r - 1.0f) < 1e-6f && std::abs(b - 1.0f) < 1e-6f)
    {
        return src;
    }

    const ConstImageView v = src.constView();
    if (v.width <= 0 || v.height <= 0)
        return src;

    ImageData dst = makeImageData(v.width, v.height, src.format);
    ImageBuffer d = dst.view();

    const bool isGray = (src.format == PixelFormat::Grayscale8);
    std::array<uint8_t, 256> lutR{};
    std::array<uint8_t, 256> lutG{};
    std::array<uint8_t, 256> lutB{};
    buildCombinedLuts(adj, f, g, r, b, isGray, lutR, lutG, lutB);
    applyCombinedLuts(v, d, lutR, lutG, lutB, src.format, v.channelsPerPixel());
    return dst;
}
