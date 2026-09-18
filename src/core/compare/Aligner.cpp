#include "core/compare/Aligner.h"
#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define MV_ALIGNER_SSE2 1
#endif

namespace mviewer
{
namespace
{

// Convert any RGB/RGBA/BGR image to an 8-bit luminance gray image.
ImageData toGray(const ImageData &src)
{
    if (src.isNull())
        return ImageData{};
    if (src.format == PixelFormat::Grayscale8)
        return src;

    ImageData g = makeImageData(src.width, src.height, PixelFormat::Grayscale8);
    if (g.isNull())
        return ImageData{};

    const ImageBuffer v = src.view();
    const ImageBuffer gv = g.view();
    const int cpp = src.channelsPerPixel();
    const bool isBgr = (src.format == PixelFormat::BGR24 || src.format == PixelFormat::BGRA32);

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
                const uint8_t r = isBgr ? p[2] : p[0];
                const uint8_t gVal = p[1];
                const uint8_t b = isBgr ? p[0] : p[2];
                grow[x] = static_cast<uint8_t>(std::clamp(luminance(r, gVal, b), 0, 255));
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

    // Always downscale from a single-channel Grayscale8 representation so multi-byte
    // color channels are not misinterpreted as coordinate steps when scale > 1.
    ImageData gray = (src.format == PixelFormat::Grayscale8) ? src : toGray(src);
    if (gray.isNull() || scale <= 1)
        return gray;

    const int w = gray.width, h = gray.height;
    const int dw = std::max(1, w / scale);
    const int dh = std::max(1, h / scale);
    ImageData d = makeImageData(dw, dh, PixelFormat::Grayscale8);
    if (d.isNull())
        return ImageData{};

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

    double bestSAD = std::numeric_limits<double>::max();
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
                int x = colLo;
#if defined(MV_ALIGNER_SSE2)
                __m128i vsum = _mm_setzero_si128();
                for (; x + 16 <= colHi; x += 16)
                {
                    const __m128i vr =
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(rp + (x - colLo)));
                    const __m128i vm =
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(mp + (x - colLo)));
                    const __m128i vdiff = _mm_sad_epu8(vr, vm);
                    vsum = _mm_add_epi64(vsum, vdiff);
                }
                alignas(16) int64_t laneSums[2];
                _mm_store_si128(reinterpret_cast<__m128i *>(laneSums), vsum);
                sad += laneSums[0] + laneSums[1];
#endif
                for (; x < colHi; ++x)
                {
                    const int d = static_cast<int>(rp[x - colLo]) - static_cast<int>(mp[x - colLo]);
                    sad += d < 0 ? -d : d;
                }
            }
            const double avg = static_cast<double>(sad) / static_cast<double>(overlap);
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
    if (out.isNull())
        return ImageData{};

    const ImageBuffer v = src.view();
    const ImageBuffer ov = out.view();
    const int cpp = src.channelsPerPixel();
    const size_t pixelBytes = static_cast<size_t>(cpp);

    const int dstX0 = std::max(0, dx);
    const int dstX1 = std::min(w, w + dx);
    const int dstY0 = std::max(0, dy);
    const int dstY1 = std::min(h, h + dy);

    if (dstX0 >= dstX1 || dstY0 >= dstY1)
    {
        for (int y = 0; y < h; ++y)
        {
            uint8_t *dstRow = ov.data + static_cast<size_t>(y) * ov.stride();
            std::memset(dstRow, fill, static_cast<size_t>(w) * pixelBytes);
        }
        return out;
    }

    const int srcX0 = dstX0 - dx;
    const int srcY0 = dstY0 - dy;
    const size_t copyRowBytes = static_cast<size_t>(dstX1 - dstX0) * pixelBytes;

    for (int y = 0; y < h; ++y)
    {
        uint8_t *dstRow = ov.data + static_cast<size_t>(y) * ov.stride();
        if (y < dstY0 || y >= dstY1)
        {
            std::memset(dstRow, fill, static_cast<size_t>(w) * pixelBytes);
        }
        else
        {
            const int sy = srcY0 + (y - dstY0);
            const uint8_t *srcRow = v.data + static_cast<size_t>(sy) * v.stride();

            if (dstX0 > 0)
                std::memset(dstRow, fill, static_cast<size_t>(dstX0) * pixelBytes);

            if (copyRowBytes > 0)
                std::memcpy(dstRow + static_cast<size_t>(dstX0) * pixelBytes,
                            srcRow + static_cast<size_t>(srcX0) * pixelBytes, copyRowBytes);

            if (dstX1 < w)
                std::memset(dstRow + static_cast<size_t>(dstX1) * pixelBytes, fill,
                            static_cast<size_t>(w - dstX1) * pixelBytes);
        }
    }
    return out;
}

} // namespace mviewer
