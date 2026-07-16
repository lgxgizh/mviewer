#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

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
    uint8_t* data = nullptr;
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

    bool isNull() const { return data == nullptr || width <= 0 || height <= 0; }

    ptrdiff_t stride() const { return static_cast<ptrdiff_t>(width) * channelsPerPixel(); }
};

struct ImageData
{
    std::shared_ptr<uint8_t[]> buffer;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB24;

    bool isNull() const { return !buffer || width <= 0 || height <= 0; }

    ImageBuffer view() const
    {
        ImageBuffer b;
        b.data = buffer.get();
        b.width = width;
        b.height = height;
        b.format = format;
        return b;
    }

    ptrdiff_t stride() const { return static_cast<ptrdiff_t>(width) * channelsPerPixel(); }

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

// Rotate an RGB/RGBA image 90 degrees clockwise. Pure std implementation
// (no Qt). Returns an empty ImageData on invalid input. Channels preserved
// (RGB24 -> RGB24, RGBA32 -> RGBA32); Grayscale8 -> Grayscale8.
inline ImageData rotate90CW(const ImageData& src)
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
            const uint8_t* sp = v.data + static_cast<size_t>(y) * v.stride() +
                                 static_cast<size_t>(x) * cpp;
            // 90 CW: dst (x', y') where x' = h-1-y, y' = x.
            const int dx = h - 1 - y;
            const int dy = x;
            uint8_t* dp = dv.data + static_cast<size_t>(dy) * dv.stride() +
                          static_cast<size_t>(dx) * cpp;
            for (int c = 0; c < cpp; ++c)
                dp[c] = sp[c];
        }
    }
    return dst;
}
