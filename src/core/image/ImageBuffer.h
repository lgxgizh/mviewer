#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

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

    int channelsPerPixel() const noexcept
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

    size_t byteSize() const noexcept
    {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(channelsPerPixel());
    }

    bool isNull() const noexcept
    {
        return data == nullptr || width <= 0 || height <= 0;
    }

    ptrdiff_t stride() const noexcept
    {
        // Multiply in a wider type to avoid int overflow for extreme widths
        // (e.g. DICOM pathology images > 100 000 px).
        return static_cast<int64_t>(width) * static_cast<int64_t>(channelsPerPixel());
    }
};

struct ImageData
{
    // Pixels owned by a std::vector, shared via shared_ptr. This restores
    // the cheap-copy semantics the old std::shared_ptr<uint8_t[]> had
    // (copying an ImageData aliases the buffer instead of deep-copying
    // all pixels), while avoiding the shared_ptr<T[]> ARRAY
    // specialization that crashes under MSVC /fsanitize=address (STL
    // control-block instrumentation conflict). shared_ptr<vector> is the
    // fully-supported, AddressSanitizer-clean form.
    std::shared_ptr<std::vector<uint8_t>> buffer;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB24;

    bool isNull() const noexcept
    {
        return !buffer || buffer->empty() || width <= 0 || height <= 0;
    }

    ImageBuffer view() const
    {
        ImageBuffer b;
        // Non-owning alias of the pixel bytes. The source ImageData
        // owns the buffer; this view is valid only while the owner is
        // alive. const_cast: buffer->data() is const here (view() is
        // const), but ImageBuffer::data is intentionally non-const for
        // in-place pixel access by analyzers/engines that hold the
        // alive owner.
        b.data = const_cast<uint8_t *>(buffer->data());
        b.width = width;
        b.height = height;
        b.format = format;
        return b;
    }
    ptrdiff_t stride() const noexcept
    {
        return static_cast<int64_t>(width) * static_cast<int64_t>(channelsPerPixel());
    }

    size_t byteSize() const noexcept
    {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(channelsPerPixel());
    }

    int channelsPerPixel() const noexcept
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
    d.buffer = std::make_shared<std::vector<uint8_t>>(bytes);
    d.width = w;
    d.height = h;
    d.format = fmt;
    return d;
}

// Canonical RGBA value of a single pixel. `valid` is false when the sample is
// out of bounds, the image is null, or the backing buffer is truncated — the
// sampler never dereferences memory in those cases.
struct PixelRGBA
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    bool valid = false;
};

// Format-aware single-pixel sampler. Reads the pixel at (x,y) and canonicalises
// it to RGBA regardless of the storage format:
//   RGB24     r=p[0] g=p[1] b=p[2] a=255
//   RGBA32    r=p[0] g=p[1] b=p[2] a=p[3]
//   BGR24     r=p[2] g=p[1] b=p[0] a=255
//   BGRA32    r=p[2] g=p[1] b=p[0] a=p[3]
//   Grayscale8 r=g=b=p[0] a=255
// Validates null image, negative / out-of-range coordinates, and that the whole
// pixel actually fits in the backing vector (so a malformed/truncated buffer
// yields valid=false instead of an out-of-bounds read). O(1), no allocations.
inline PixelRGBA samplePixel(const ImageData &img, int x, int y) noexcept
{
    PixelRGBA out;
    if (img.isNull() || x < 0 || y < 0 || x >= img.width || y >= img.height)
        return out;
    const int cpp = img.channelsPerPixel();
    const size_t idx =
        static_cast<size_t>(y) * static_cast<size_t>(img.width) * static_cast<size_t>(cpp) +
        static_cast<size_t>(x) * static_cast<size_t>(cpp);
    if (idx + static_cast<size_t>(cpp) > img.buffer->size())
        return out;
    const uint8_t *p = img.buffer->data() + idx;
    switch (img.format)
    {
    case PixelFormat::RGB24:
        out.r = p[0];
        out.g = p[1];
        out.b = p[2];
        out.a = 255;
        break;
    case PixelFormat::RGBA32:
        out.r = p[0];
        out.g = p[1];
        out.b = p[2];
        out.a = p[3];
        break;
    case PixelFormat::BGR24:
        out.r = p[2];
        out.g = p[1];
        out.b = p[0];
        out.a = 255;
        break;
    case PixelFormat::BGRA32:
        out.r = p[2];
        out.g = p[1];
        out.b = p[0];
        out.a = p[3];
        break;
    case PixelFormat::Grayscale8:
        out.r = p[0];
        out.g = p[0];
        out.b = p[0];
        out.a = 255;
        break;
    default:
        return out;
    }
    out.valid = true;
    return out;
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
            v.data + static_cast<size_t>(y0 + y) * v.stride() + static_cast<size_t>(x0) * cpp;
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
