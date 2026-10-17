#include "core/compare/Aligner.h"
#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "core/simd/CpuFeatures.h"

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

// Coarse SAD grid search over downscaled thumbnails.
AlignOffset coarseSearch(const ImageBuffer &rv, const ImageBuffer &mv, int maxShift)
{
    double bestSAD = std::numeric_limits<double>::infinity();
    AlignOffset best{0, 0};

    for (int dy = -maxShift; dy <= maxShift; ++dy)
    {
        const int rowLo = std::max(0, dy);
        const int rowHi = std::min(rv.height, mv.height + dy);
        if (rowHi <= rowLo)
            continue;
        for (int dx = -maxShift; dx <= maxShift; ++dx)
        {
            const int colLo = std::max(0, dx);
            const int colHi = std::min(rv.width, mv.width + dx);
            if (colHi <= colLo)
                continue;
            const int overlap = (colHi - colLo) * (rowHi - rowLo);
            if (overlap <= 0)
                continue;

            const long long maxAllowedSad = (bestSAD == std::numeric_limits<double>::infinity())
                                                ? std::numeric_limits<long long>::max()
                                                : static_cast<long long>(bestSAD * overlap);

            long long sad = 0;
            const int len = colHi - colLo;
            for (int y = rowLo; y < rowHi; ++y)
            {
                const uint8_t *rp = rv.data + static_cast<size_t>(y) * rv.stride() + colLo;
                const uint8_t *mp =
                    mv.data + static_cast<size_t>(y - dy) * mv.stride() + (colLo - dx);
                int x = 0;
#if defined(MV_ARCH_X86) &&                                                                        \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
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

                if (sad >= maxAllowedSad)
                    break;
            }
            const double avg = static_cast<double>(sad) / static_cast<double>(overlap);
            if (avg < bestSAD)
            {
                bestSAD = avg;
                best = {dx, dy};
            }
        }
    }
    return best;
}

// Fine 1-pixel precision SAD search around coarse offset on full-resolution rasters.
AlignOffset fineSearch(const ImageBuffer &frv, const ImageBuffer &fmv, int coarseDx, int coarseDy,
                       int scale, int maxShift)
{
    double fineBestSAD = std::numeric_limits<double>::infinity();
    AlignOffset fineBest{coarseDx, coarseDy};

    const int fineMinY = std::max(-maxShift * scale, coarseDy - scale);
    const int fineMaxY = std::min(maxShift * scale, coarseDy + scale);
    const int fineMinX = std::max(-maxShift * scale, coarseDx - scale);
    const int fineMaxX = std::min(maxShift * scale, coarseDx + scale);

    const int maxW = std::min(frv.width, fmv.width);
    const int maxH = std::min(frv.height, fmv.height);
    const int winW = std::min(maxW, 512);
    const int winH = std::min(maxH, 512);
    const int startX = (maxW - winW) / 2;
    const int startY = (maxH - winH) / 2;

    for (int dy = fineMinY; dy <= fineMaxY; ++dy)
    {
        const int rowLo = std::max({startY, startY + dy, 0, dy});
        const int rowHi =
            std::min({startY + winH, startY + winH + dy, frv.height, fmv.height + dy});
        if (rowHi <= rowLo)
            continue;
        for (int dx = fineMinX; dx <= fineMaxX; ++dx)
        {
            const int colLo = std::max({startX, startX + dx, 0, dx});
            const int colHi =
                std::min({startX + winW, startX + winW + dx, frv.width, fmv.width + dx});
            if (colHi <= colLo)
                continue;

            const int len = colHi - colLo;
            const int overlap = len * (rowHi - rowLo);
            if (overlap <= 0)
                continue;

            const long long maxAllowedSad = (fineBestSAD == std::numeric_limits<double>::infinity())
                                                ? std::numeric_limits<long long>::max()
                                                : static_cast<long long>(fineBestSAD * overlap);

            long long sad = 0;
            for (int y = rowLo; y < rowHi; ++y)
            {
                const uint8_t *rp = frv.data + static_cast<size_t>(y) * frv.stride() + colLo;
                const uint8_t *mp =
                    fmv.data + static_cast<size_t>(y - dy) * fmv.stride() + (colLo - dx);
                int x = 0;
#if defined(MV_ARCH_X86) &&                                                                        \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
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

                if (sad >= maxAllowedSad)
                    break;
            }

            const double avg = static_cast<double>(sad) / static_cast<double>(overlap);
            if (avg < fineBestSAD)
            {
                fineBestSAD = avg;
                fineBest = {dx, dy};
            }
        }
    }
    return fineBest;
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

    const AlignOffset best = coarseSearch(rg.view(), mg.view(), maxShift);
    const int coarseDx = best.x * scale;
    const int coarseDy = best.y * scale;

    if (scale <= 1)
        return {coarseDx, coarseDy};

    // Coarse-to-fine 1-pixel precision search:
    // Refine around (coarseDx, coarseDy) in the range [-scale, +scale]
    // at exact 1-pixel resolution using full-resolution grayscale images.
    ImageData fullRefGray = (ref.format == PixelFormat::Grayscale8) ? ref : toGray(ref);
    ImageData fullMovGray = (moving.format == PixelFormat::Grayscale8) ? moving : toGray(moving);
    if (fullRefGray.isNull() || fullMovGray.isNull())
        return {coarseDx, coarseDy};

    return fineSearch(fullRefGray.view(), fullMovGray.view(), coarseDx, coarseDy, scale, maxShift);
}

ImageData Aligner::shift(const ImageData &src, int dx, int dy, uint8_t fill)
{
    if (src.isNull())
        return ImageData{};
    if (dx == 0 && dy == 0)
        return src;
    const int w = src.width, h = src.height;
    ImageData out = makeImageData(w, h, src.format);
    if (out.isNull())
        return ImageData{};
    const ImageBuffer v = src.view();
    const ImageBuffer ov = out.view();
    const int cpp = src.channelsPerPixel();
    const size_t rowBytes = static_cast<size_t>(w) * cpp;

    const int srcX0 = std::max(0, -dx);
    const int srcX1 = std::min(w, w - dx);
    const int dstX0 = srcX0 + dx;
    const int copyPixels = std::max(0, srcX1 - srcX0);
    const size_t copyBytes = static_cast<size_t>(copyPixels) * cpp;

    for (int y = 0; y < h; ++y)
    {
        uint8_t *dst = ov.data + static_cast<size_t>(y) * ov.stride();
        const int sy = y - dy;
        if (sy < 0 || sy >= h || copyPixels <= 0)
        {
            std::memset(dst, fill, rowBytes);
            continue;
        }
        const uint8_t *srcRow = v.data + static_cast<size_t>(sy) * v.stride();
        if (dstX0 > 0)
            std::memset(dst, fill, static_cast<size_t>(dstX0) * cpp);
        std::memcpy(dst + static_cast<size_t>(dstX0) * cpp,
                    srcRow + static_cast<size_t>(srcX0) * cpp, copyBytes);
        const int dstX1 = dstX0 + copyPixels;
        if (dstX1 < w)
            std::memset(dst + static_cast<size_t>(dstX1) * cpp, fill,
                        static_cast<size_t>(w - dstX1) * cpp);
    }
    return out;
}

} // namespace mviewer
