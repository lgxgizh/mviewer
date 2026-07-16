#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "domain/Selection.h"

enum class PixelFormat
{
    RGB24,
    RGBA32,
    BGR24,
    BGRA32,
    Grayscale8
};

struct ImageBuffer
{
    uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB24;

    int channelsPerPixel() const
    {
        switch (format)
        {
        case PixelFormat::RGB24:
            return 3;
        case PixelFormat::RGBA32:
            return 4;
        case PixelFormat::BGR24:
            return 3;
        case PixelFormat::BGRA32:
            return 4;
        case PixelFormat::Grayscale8:
            return 1;
        }
        return 3;
    }

    size_t byteSize() const
    {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(channelsPerPixel());
    }

    bool isNull() const
    {
        return data == nullptr || width <= 0 || height <= 0;
    }

    ptrdiff_t stride() const
    {
        return static_cast<ptrdiff_t>(width) * channelsPerPixel();
    }
};

struct ImageData
{
    std::shared_ptr<uint8_t[]> buffer;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB24;

    bool isNull() const
    {
        return !buffer || width <= 0 || height <= 0;
    }

    ImageBuffer view() const
    {
        ImageBuffer b;
        b.data = buffer.get();
        b.width = width;
        b.height = height;
        b.format = format;
        return b;
    }

    ptrdiff_t stride() const
    {
        return static_cast<ptrdiff_t>(width) * channelsPerPixel();
    }

    size_t byteSize() const
    {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(channelsPerPixel());
    }

    int channelsPerPixel() const
    {
        switch (format)
        {
        case PixelFormat::RGB24:
            return 3;
        case PixelFormat::RGBA32:
            return 4;
        case PixelFormat::BGR24:
            return 3;
        case PixelFormat::BGRA32:
            return 4;
        case PixelFormat::Grayscale8:
            return 1;
        }
        return 3;
    }
};

inline ImageData makeImageData(int w, int h, PixelFormat fmt)
{
    const int cpp = (fmt == PixelFormat::RGBA32 || fmt == PixelFormat::BGRA32)
                        ? 4
                        : (fmt == PixelFormat::Grayscale8 ? 1 : 3);
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(cpp);
    ImageData d;
    d.buffer = std::shared_ptr<uint8_t[]>(new uint8_t[bytes]);
    d.width = w;
    d.height = h;
    d.format = fmt;
    return d;
}

inline int luminance(uint8_t r, uint8_t g, uint8_t b)
{
    return (int)(0.299 * r + 0.587 * g + 0.114 * b);
}

// Crop a rectangular region from an image. Pure std implementation (no Qt).
// Returns an empty ImageData on invalid input or an out-of-bounds / empty
// selection. The selection is clamped to the source bounds, so a partially
// out-of-bounds ROI yields the valid intersection. Channels/format preserved.
inline ImageData cropRegion(const ImageData &src, const mviewer::domain::Selection &sel)
{
    if (src.isNull() || sel.isEmpty())
        return ImageData{};
    const int cpp = src.channelsPerPixel();
    const int sw = src.width;
    const int sh = src.height;

    // Clamp selection to source bounds.
    const int x0 = std::max(0, sel.x);
    const int y0 = std::max(0, sel.y);
    const int x1 = std::min(sw, sel.x + sel.width);
    const int y1 = std::min(sh, sel.y + sel.height);
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    if (cw <= 0 || ch <= 0)
        return ImageData{};

    ImageData dst = makeImageData(cw, ch, src.format);
    const ImageBuffer v = src.view();
    const ImageBuffer dv = dst.view();
    for (int y = 0; y < ch; ++y)
    {
        const uint8_t *sp =
            v.data + static_cast<size_t>(y0 + y) * v.stride() +
            static_cast<size_t>(x0) * cpp;
        uint8_t *dp = dv.data + static_cast<size_t>(y) * dv.stride();
        std::memcpy(dp, sp, static_cast<size_t>(cw) * static_cast<size_t>(cpp));
    }
    return dst;
}

// Rotate an RGB/RGBA image 90 degrees clockwise. Pure std implementation
// (no Qt). Returns an empty ImageData on invalid input. Channels preserved
// (RGB24 -> RGB24, RGBA32 -> RGBA32); Grayscale8 -> Grayscale8.
inline ImageData rotate90CW(const ImageData &src)
{
    if (src.isNull())
        return ImageData{};
    const int cpp = src.channelsPerPixel();
    const int w = src.width;
    const int h = src.height;
    // dst is h x w.
    ImageData dst = makeImageData(h, w, src.format);
    const ImageBuffer v = src.view();
    const ImageBuffer dv = dst.view();
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const uint8_t *sp =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            // 90 CW: dst (x', y') where x' = h-1-y, y' = x.
            const int dx = h - 1 - y;
            const int dy = x;
            uint8_t *dp =
                dv.data + static_cast<size_t>(dy) * dv.stride() + static_cast<size_t>(dx) * cpp;
            for (int c = 0; c < cpp; ++c)
                dp[c] = sp[c];
        }
    }
    return dst;
}
