#include "core/compare/Aligner.h"
#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mviewer
{
namespace
{

// Convert any RGB/RGBA image to an 8-bit luminance gray image.
ImageData toGray(const ImageData &src)
{
    if (src.isNull())
        return ImageData{};
    ImageData g = makeImageData(src.width, src.height, PixelFormat::Grayscale8);
    const ImageBuffer v = src.view();
    const ImageBuffer gv = g.view();
    const int cpp = src.channelsPerPixel();
    for (int y = 0; y < src.height; ++y)
    {
        const uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        uint8_t *grow = gv.data + static_cast<size_t>(y) * gv.stride();
        if (cpp == 1)
        {
            std::memcpy(grow, row, static_cast<size_t>(src.width));
        }
        else
        {
            for (int x = 0; x < src.width; ++x)
            {
                const uint8_t *p = row + static_cast<size_t>(x) * cpp;
                grow[x] = static_cast<uint8_t>(std::clamp(luminance(p[0], p[1], p[2]), 0, 255));
            }
        }
    }
    return g;
}

// Box-average downscale to a gray image sampled every `scale` pixels.
ImageData downscaleBy(const ImageData &src, int scale)
{
    if (src.isNull() || scale <= 1)
        return toGray(src);
    const int w = src.width, h = src.height;
    const int dw = std::max(1, w / scale);
    const int dh = std::max(1, h / scale);
    ImageData d = makeImageData(dw, dh, PixelFormat::Grayscale8);
    const ImageBuffer v = src.view();
    const ImageBuffer dv = d.view();
    for (int y = 0; y < dh; ++y)
    {
        for (int x = 0; x < dw; ++x)
        {
            long sum = 0;
            int cnt = 0;
            for (int yy = 0; yy < scale; ++yy)
            {
                const int sy = y * scale + yy;
                if (sy >= h)
                    break;
                const uint8_t *row = v.data + static_cast<size_t>(sy) * v.stride();
                for (int xx = 0; xx < scale; ++xx)
                {
                    const int sx = x * scale + xx;
                    if (sx >= w)
                        break;
                    sum += row[sx];
                    ++cnt;
                }
            }
            dv.data[static_cast<size_t>(y) * dv.stride() + x] =
                cnt ? static_cast<uint8_t>(sum / cnt) : 0;
        }
    }
    return d;
}

} // namespace

AlignOffset Aligner::estimate(const ImageData &ref, const ImageData &moving, int maxShift)
{
    if (ref.isNull() || moving.isNull())
        return {0, 0};

    // Downscale both by the same factor for a consistent offset mapping.
    const int fullMax =
        std::max(std::max(ref.width, ref.height), std::max(moving.width, moving.height));
    const int scale = std::max(1, fullMax / 256);
    ImageData rg = downscaleBy(ref, scale);
    ImageData mg = downscaleBy(moving, scale);
    if (rg.isNull() || mg.isNull())
        return {0, 0};

    const ImageBuffer rv = rg.view();
    const ImageBuffer mv = mg.view();
    const int W = rg.width, H = rg.height;

    long long bestSAD = std::numeric_limits<long long>::max();
    AlignOffset best{0, 0};

    for (int dy = -maxShift; dy <= maxShift; ++dy)
    {
        const int rowLo = std::max(0, dy);
        const int rowHi = std::min(H, H + dy);
        if (rowHi <= rowLo)
            continue;
        for (int dx = -maxShift; dx <= maxShift; ++dx)
        {
            const int colLo = std::max(0, dx);
            const int colHi = std::min(W, W + dx);
            if (colHi <= colLo)
                continue;
            long long sad = 0;
            const int overlap = (colHi - colLo) * (rowHi - rowLo);
            for (int y = rowLo; y < rowHi; ++y)
            {
                const uint8_t *rp = rv.data + static_cast<size_t>(y) * rv.stride() + colLo;
                const uint8_t *mp =
                    mv.data + static_cast<size_t>(y - dy) * mv.stride() + (colLo - dx);
                for (int x = colLo; x < colHi; ++x)
                {
                    const int d = static_cast<int>(rp[x - colLo]) - static_cast<int>(mp[x - colLo]);
                    sad += d < 0 ? -d : d;
                }
            }
            const long long avg = sad / overlap;
            if (avg < bestSAD)
            {
                bestSAD = avg;
                best = {dx, dy};
            }
        }
    }
    best.x *= scale;
    best.y *= scale;
    return best;
}

ImageData Aligner::shift(const ImageData &src, int dx, int dy, uint8_t fill)
{
    if (src.isNull())
        return ImageData{};
    const int w = src.width, h = src.height;
    ImageData out = makeImageData(w, h, src.format);
    const ImageBuffer v = src.view();
    const ImageBuffer ov = out.view();
    const int cpp = src.channelsPerPixel();
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int sx = x - dx;
            const int sy = y - dy;
            uint8_t *dst =
                ov.data + static_cast<size_t>(y) * ov.stride() + static_cast<size_t>(x) * cpp;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h)
            {
                const uint8_t *p =
                    v.data + static_cast<size_t>(sy) * v.stride() + static_cast<size_t>(sx) * cpp;
                for (int c = 0; c < cpp; ++c)
                    dst[c] = p[c];
            }
            else
            {
                for (int c = 0; c < cpp; ++c)
                    dst[c] = fill;
            }
        }
    }
    return out;
}

} // namespace mviewer
