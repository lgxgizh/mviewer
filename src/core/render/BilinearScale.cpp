#include "core/simd/CpuFeatures.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#define MV_BILINEAR_SSE2 1
#endif

namespace mviewer::core::render_detail
{
namespace
{

struct BilinearX
{
    int x0;
    int fx;
    int invFx;
};

QImage nearestFallback(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    for (int y = 0; y < th; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const int sy = std::min(sh - 1, (y * sh) / th);
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        for (int x = 0; x < tw; ++x)
        {
            const int sx = std::min(sw - 1, (x * sw) / tw);
            line[x] = sline[sx];
        }
    }
    return out;
}

#if defined(MV_BILINEAR_SSE2)
// SSE2-compatible 4x int32 lane multiply (even/odd via mul_epu32).
inline __m128i mulloEpi32Sse2(const __m128i a, const __m128i b)
{
    const __m128i lo = _mm_mul_epu32(a, b);
    const __m128i hi = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(lo, _MM_SHUFFLE(0, 0, 2, 0)),
                              _mm_shuffle_epi32(hi, _MM_SHUFFLE(0, 0, 2, 0)));
}

inline __m128i bilinearBlendChannelSse2(const __m128i vw00, const __m128i vw10, const __m128i vw01,
                                        const __m128i vw11, const int *c00, const int *c10,
                                        const int *c01, const int *c11)
{
    const __m128i v128 = _mm_set1_epi32(128);
    __m128i v = mulloEpi32Sse2(vw00, _mm_load_si128(reinterpret_cast<const __m128i *>(c00)));
    v = _mm_add_epi32(v,
                      mulloEpi32Sse2(vw10, _mm_load_si128(reinterpret_cast<const __m128i *>(c10))));
    v = _mm_add_epi32(v,
                      mulloEpi32Sse2(vw01, _mm_load_si128(reinterpret_cast<const __m128i *>(c01))));
    v = _mm_add_epi32(v,
                      mulloEpi32Sse2(vw11, _mm_load_si128(reinterpret_cast<const __m128i *>(c11))));
    return _mm_srai_epi32(_mm_add_epi32(v, v128), 8);
}

// Returns the next x after the SSE2 4-wide run (scalar tail starts there).
int fillBilinearRowSse2(QRgb *line, int tw, int w0y, int w1y, const QRgb *sl0, const QRgb *sl1,
                        const std::vector<BilinearX> &xTab)
{
    int x = 0;
    for (; x + 4 <= tw; x += 4)
    {
        alignas(16) int w00a[4], w10a[4], w01a[4], w11a[4];
        alignas(16) int r00[4], g00[4], b00[4];
        alignas(16) int r10[4], g10[4], b10[4];
        alignas(16) int r01[4], g01[4], b01[4];
        alignas(16) int r11[4], g11[4], b11[4];
        for (int k = 0; k < 4; ++k)
        {
            const auto &tab = xTab[static_cast<size_t>(x) + static_cast<size_t>(k)];
            w00a[k] = (tab.invFx * w0y) >> 8;
            w10a[k] = (tab.fx * w0y) >> 8;
            w01a[k] = (tab.invFx * w1y) >> 8;
            w11a[k] = (tab.fx * w1y) >> 8;
            const int x0 = tab.x0;
            const QRgb p00 = sl0[x0];
            const QRgb p10 = sl0[x0 + 1];
            const QRgb p01 = sl1[x0];
            const QRgb p11 = sl1[x0 + 1];
            r00[k] = qRed(p00);
            g00[k] = qGreen(p00);
            b00[k] = qBlue(p00);
            r10[k] = qRed(p10);
            g10[k] = qGreen(p10);
            b10[k] = qBlue(p10);
            r01[k] = qRed(p01);
            g01[k] = qGreen(p01);
            b01[k] = qBlue(p01);
            r11[k] = qRed(p11);
            g11[k] = qGreen(p11);
            b11[k] = qBlue(p11);
        }

        const __m128i vw00 = _mm_load_si128(reinterpret_cast<const __m128i *>(w00a));
        const __m128i vw10 = _mm_load_si128(reinterpret_cast<const __m128i *>(w10a));
        const __m128i vw01 = _mm_load_si128(reinterpret_cast<const __m128i *>(w01a));
        const __m128i vw11 = _mm_load_si128(reinterpret_cast<const __m128i *>(w11a));
        alignas(16) int rr[4], gg[4], bb[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(rr),
                        bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, r00, r10, r01, r11));
        _mm_store_si128(reinterpret_cast<__m128i *>(gg),
                        bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, g00, g10, g01, g11));
        _mm_store_si128(reinterpret_cast<__m128i *>(bb),
                        bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, b00, b10, b01, b11));
        for (int k = 0; k < 4; ++k)
            line[x + k] = qRgb(std::clamp(rr[k], 0, 255), std::clamp(gg[k], 0, 255),
                               std::clamp(bb[k], 0, 255));
    }
    return x;
}
#endif

void fillBilinearRowScalar(QRgb *line, int x0, int tw, int w0y, int w1y, const QRgb *sl0,
                           const QRgb *sl1, const std::vector<BilinearX> &xTab)
{
    for (int x = x0; x < tw; ++x)
    {
        const auto &tab = xTab[static_cast<size_t>(x)];
        const int w00 = (tab.invFx * w0y) >> 8;
        const int w10 = (tab.fx * w0y) >> 8;
        const int w01 = (tab.invFx * w1y) >> 8;
        const int w11 = (tab.fx * w1y) >> 8;
        const int sx0 = tab.x0;

        const QRgb p00 = sl0[sx0];
        const QRgb p10 = sl0[sx0 + 1];
        const QRgb p01 = sl1[sx0];
        const QRgb p11 = sl1[sx0 + 1];

        const int r =
            (w00 * qRed(p00) + w10 * qRed(p10) + w01 * qRed(p01) + w11 * qRed(p11) + 128) >> 8;
        const int g =
            (w00 * qGreen(p00) + w10 * qGreen(p10) + w01 * qGreen(p01) + w11 * qGreen(p11) + 128) >>
            8;
        const int b =
            (w00 * qBlue(p00) + w10 * qBlue(p10) + w01 * qBlue(p01) + w11 * qBlue(p11) + 128) >> 8;
        line[x] = qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    }
}

QImage bilinearQImpl(const QImage &src, const QSize &target)
{
    if (src.isNull() || target.width() <= 0 || target.height() <= 0)
        return QImage();

    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();

    if (sw <= 1 || sh <= 1)
        return nearestFallback(src, target);

    QImage out(target, QImage::Format_RGB32);
    const double rx = static_cast<double>(sw) / static_cast<double>(tw);
    const double ry = static_cast<double>(sh) / static_cast<double>(th);

    std::vector<BilinearX> xTab(static_cast<size_t>(tw));
    for (int x = 0; x < tw; ++x)
    {
        const double sx = (static_cast<double>(x) + 0.5) * rx - 0.5;
        const int x0 = std::max(0, std::min(sw - 2, static_cast<int>(std::floor(sx))));
        const double fxD = std::max(0.0, sx - std::floor(sx));
        const int fx = static_cast<int>(std::round(fxD * 256.0));
        xTab[static_cast<size_t>(x)] = {x0, fx, 256 - fx};
    }

#if defined(MV_BILINEAR_SSE2)
    const bool useSse2 = mviewer::core::CpuFeatures::hasSSE2();
#else
    const bool useSse2 = false;
#endif

    for (int y = 0; y < th; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double sy = (static_cast<double>(y) + 0.5) * ry - 0.5;
        const int y0 = std::max(0, std::min(sh - 2, static_cast<int>(std::floor(sy))));
        const double fyD = std::max(0.0, sy - std::floor(sy));
        const int fy = static_cast<int>(std::round(fyD * 256.0));
        const int w0y = 256 - fy;
        const int w1y = fy;
        const QRgb *sl0 = reinterpret_cast<const QRgb *>(src.constScanLine(y0));
        const QRgb *sl1 = reinterpret_cast<const QRgb *>(src.constScanLine(y0 + 1));

        int x = 0;
#if defined(MV_BILINEAR_SSE2)
        if (useSse2)
            x = fillBilinearRowSse2(line, tw, w0y, w1y, sl0, sl1, xTab);
#endif
        (void)useSse2;
        fillBilinearRowScalar(line, x, tw, w0y, w1y, sl0, sl1, xTab);
    }
    return out;
}

} // namespace

QImage bilinearQ(const QImage &src, const QSize &target)
{
    return bilinearQImpl(src, target);
}

} // namespace mviewer::core::render_detail
