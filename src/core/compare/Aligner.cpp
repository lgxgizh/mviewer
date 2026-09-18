#include "core/compare/Aligner.h"
#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#if defined(_MSC_VER)
#include <immintrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <immintrin.h>
#endif

namespace mviewer
{
namespace
{

// Convert any RGB/RGBA/BGR/BGRA image to an 8-bit luminance gray image.
ImageData toGray(const ImageData &src)
{
    if (src.isNull())
        return ImageData{};
    if (src.format == PixelFormat::Grayscale8)
        return src;
    ImageData g = makeImageData(src.width, src.height, PixelFormat::Grayscale8);
    const ImageBuffer v = src.view();
    const ImageBuffer gv = g.view();
    const int cpp = src.channelsPerPixel();
    const bool isBgr = (src.format == PixelFormat::BGR24 || src.format == PixelFormat::BGRA32);
    const int rOffset = isBgr ? 2 : 0;
    const int bOffset = isBgr ? 0 : 2;
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
            const uint8_t *p = row;
            for (int x = 0; x < src.width; ++x, p += cpp)
            {
                grow[x] = static_cast<uint8_t>(luminance(p[rOffset], p[1], p[bOffset]));
            }
        }
    }
    return g;
}

// Box-average downscale to a gray image sampled every `scale` pixels.
ImageData downscaleBy(const ImageData &src, int scale)
{
    if (src.isNull())
        return ImageData{};
    ImageData gray = (src.format == PixelFormat::Grayscale8) ? src : toGray(src);
    if (gray.isNull() || scale <= 1)
        return gray;
    const int w = gray.width, h = gray.height;
    const int dw = std::max(1, w / scale);
    const int dh = std::max(1, h / scale);
    ImageData d = makeImageData(dw, dh, PixelFormat::Grayscale8);
    const ImageBuffer v = gray.view();
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
            const int len = colHi - colLo;
            for (int y = rowLo; y < rowHi; ++y)
            {
                const uint8_t *rp = rv.data + static_cast<size_t>(y) * rv.stride() + colLo;
                const uint8_t *mp =
                    mv.data + static_cast<size_t>(y - dy) * mv.stride() + (colLo - dx);
                int x = 0;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
                __m128i vsum = _mm_setzero_si128();
                for (; x + 16 <= len; x += 16)
                {
                    const __m128i vr = _mm_loadu_si128(reinterpret_cast<const __m128i *>(rp + x));
                    const __m128i vm = _mm_loadu_si128(reinterpret_cast<const __m128i *>(mp + x));
                    vsum = _mm_add_epi64(vsum, _mm_sad_epu8(vr, vm));
                }
                alignas(16) uint64_t sums[2];
                _mm_store_si128(reinterpret_cast<__m128i *>(sums), vsum);
                sad += static_cast<long long>(sums[0] + sums[1]);
#endif
                for (; x < len; ++x)
                {
                    const int d = static_cast<int>(rp[x]) - static_cast<int>(mp[x]);
                    sad += (d < 0 ? -d : d);
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
        uint8_t *dst = ov.data + static_cast<size_t>(y) * ov.stride();
        const int sy = y - dy;
        const bool yInBounds = (sy >= 0 && sy < h);
        const uint8_t *srcRow =
            yInBounds ? (v.data + static_cast<size_t>(sy) * v.stride()) : nullptr;
        for (int x = 0; x < w; ++x, dst += cpp)
        {
            const int sx = x - dx;
            if (yInBounds && sx >= 0 && sx < w)
            {
                const uint8_t *p = srcRow + static_cast<size_t>(sx) * cpp;
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
