#include "core/render/RenderEngine.h"

#include "core/image/QtConvert.h"
#include "core/simd/CpuFeatures.h"
#include "core/trace/Trace.h"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#define MV_BILINEAR_SSE2 1
#endif

namespace
{

QImage nearestQ(const QImage &src, const QSize &target)
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

struct BilinearX
{
    int x0;
    int fx;
    int invFx;
};

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
#endif

QImage bilinearQ(const QImage &src, const QSize &target)
{
    if (src.isNull() || target.width() <= 0 || target.height() <= 0)
        return QImage();

    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();

    if (sw <= 1 || sh <= 1)
        return nearestQ(src, target);

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
        {
            for (; x + 4 <= tw; x += 4)
            {
                alignas(16) int w00a[4], w10a[4], w01a[4], w11a[4];
                alignas(16) int r00[4], g00[4], b00[4];
                alignas(16) int r10[4], g10[4], b10[4];
                alignas(16) int r01[4], g01[4], b01[4];
                alignas(16) int r11[4], g11[4], b11[4];
                for (int k = 0; k < 4; ++k)
                {
                    const auto &tab = xTab[static_cast<size_t>(x + k)];
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
                _mm_store_si128(
                    reinterpret_cast<__m128i *>(rr),
                    bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, r00, r10, r01, r11));
                _mm_store_si128(
                    reinterpret_cast<__m128i *>(gg),
                    bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, g00, g10, g01, g11));
                _mm_store_si128(
                    reinterpret_cast<__m128i *>(bb),
                    bilinearBlendChannelSse2(vw00, vw10, vw01, vw11, b00, b10, b01, b11));
                for (int k = 0; k < 4; ++k)
                    line[x + k] = qRgb(std::clamp(rr[k], 0, 255), std::clamp(gg[k], 0, 255),
                                       std::clamp(bb[k], 0, 255));
            }
        }
#endif
        for (; x < tw; ++x)
        {
            const auto &tab = xTab[static_cast<size_t>(x)];
            const int w00 = (tab.invFx * w0y) >> 8;
            const int w10 = (tab.fx * w0y) >> 8;
            const int w01 = (tab.invFx * w1y) >> 8;
            const int w11 = (tab.fx * w1y) >> 8;
            const int x0 = tab.x0;

            const QRgb p00 = sl0[x0];
            const QRgb p10 = sl0[x0 + 1];
            const QRgb p01 = sl1[x0];
            const QRgb p11 = sl1[x0 + 1];

            const int r =
                (w00 * qRed(p00) + w10 * qRed(p10) + w01 * qRed(p01) + w11 * qRed(p11) + 128) >> 8;
            const int g = (w00 * qGreen(p00) + w10 * qGreen(p10) + w01 * qGreen(p01) +
                           w11 * qGreen(p11) + 128) >>
                          8;
            const int b =
                (w00 * qBlue(p00) + w10 * qBlue(p10) + w01 * qBlue(p01) + w11 * qBlue(p11) + 128) >>
                8;
            line[x] = qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }
    return out;
}

// Bicubic interpolation kernel (Catmull-Rom, a=0.5)
static double cubicKernel(double x)
{
    x = std::abs(x);
    if (x < 1.0)
    {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    }
    else if (x < 2.0)
    {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

struct BicubicX
{
    int sxx[4];
    double wx[4];
};

QImage bicubicQ(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;

    std::vector<BicubicX> xTab(tw);
    for (int x = 0; x < tw; ++x)
    {
        const double sx = (x + 0.5) * rx - 0.5;
        const int x0 = static_cast<int>(std::floor(sx));
        for (int n = -1; n <= 2; ++n)
        {
            xTab[x].sxx[n + 1] = std::max(0, std::min(sw - 1, x0 + n));
            xTab[x].wx[n + 1] = cubicKernel(sx - (x0 + n));
        }
    }

    for (int y = 0; y < th; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        double wy[4];
        const QRgb *slines[4];
        for (int m = -1; m <= 2; ++m)
        {
            const int syy = std::max(0, std::min(sh - 1, y0 + m));
            wy[m + 1] = cubicKernel(sy - (y0 + m));
            slines[m + 1] = reinterpret_cast<const QRgb *>(src.constScanLine(syy));
        }

        for (int x = 0; x < tw; ++x)
        {
            const auto &xt = xTab[x];
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = 0; m < 4; ++m)
            {
                const double wym = wy[m];
                const QRgb *sline = slines[m];
                for (int n = 0; n < 4; ++n)
                {
                    const double w = xt.wx[n] * wym;
                    const QRgb pix = sline[xt.sxx[n]];
                    r += w * qRed(pix);
                    g += w * qGreen(pix);
                    b += w * qBlue(pix);
                    wsum += w;
                }
            }
            if (wsum > 0)
            {
                r /= wsum;
                g /= wsum;
                b /= wsum;
            }
            line[x] = qRgb(std::clamp(static_cast<int>(std::round(r)), 0, 255),
                           std::clamp(static_cast<int>(std::round(g)), 0, 255),
                           std::clamp(static_cast<int>(std::round(b)), 0, 255));
        }
    }
    return out;
}

// Lanczos kernel (3-lobe)
static constexpr double PI_CONST = 3.14159265358979323846;

static double lanczosKernel(double x)
{
    x = std::abs(x);
    if (x < 1e-6)
        return 1.0;
    if (x >= 3.0)
        return 0.0;
    const double pix = PI_CONST * x;
    return 3.0 * std::sin(pix) * std::sin(pix / 3.0) / (pix * pix);
}

struct LanczosX
{
    int sxx[5];
    double wx[5];
};

QImage lanczosQ(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;

    std::vector<LanczosX> xTab(tw);
    for (int x = 0; x < tw; ++x)
    {
        const double sx = (x + 0.5) * rx - 0.5;
        const int x0 = static_cast<int>(std::floor(sx));
        for (int n = -2; n <= 2; ++n)
        {
            xTab[x].sxx[n + 2] = std::max(0, std::min(sw - 1, x0 + n));
            xTab[x].wx[n + 2] = lanczosKernel(sx - (x0 + n));
        }
    }

    for (int y = 0; y < th; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        double wy[5];
        const QRgb *slines[5];
        for (int m = -2; m <= 2; ++m)
        {
            const int syy = std::max(0, std::min(sh - 1, y0 + m));
            wy[m + 2] = lanczosKernel(sy - (y0 + m));
            slines[m + 2] = reinterpret_cast<const QRgb *>(src.constScanLine(syy));
        }

        for (int x = 0; x < tw; ++x)
        {
            const auto &xt = xTab[x];
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = 0; m < 5; ++m)
            {
                const double wym = wy[m];
                const QRgb *sline = slines[m];
                for (int n = 0; n < 5; ++n)
                {
                    const double w = xt.wx[n] * wym;
                    const QRgb pix = sline[xt.sxx[n]];
                    r += w * qRed(pix);
                    g += w * qGreen(pix);
                    b += w * qBlue(pix);
                    wsum += w;
                }
            }
            if (wsum > 0)
            {
                r /= wsum;
                g /= wsum;
                b /= wsum;
            }
            line[x] = qRgb(std::clamp(static_cast<int>(std::round(r)), 0, 255),
                           std::clamp(static_cast<int>(std::round(g)), 0, 255),
                           std::clamp(static_cast<int>(std::round(b)), 0, 255));
        }
    }
    return out;
}

QImage scaleQ(const QImage &src, const QSize &target, RenderInterp mode)
{
    if (src.isNull() || target.width() <= 0 || target.height() <= 0)
        return QImage();
    const QImage rgb = src.convertToFormat(QImage::Format_RGB32);
    switch (mode)
    {
    case RenderInterp::Nearest:
        return nearestQ(rgb, target);
    case RenderInterp::Bilinear:
        return bilinearQ(rgb, target);
    case RenderInterp::Bicubic:
        return bicubicQ(rgb, target);
    case RenderInterp::Lanczos:
        return lanczosQ(rgb, target);
    }
    return QImage();
}

static const std::array<QRgb, 256> s_heatLut = []()
{
    std::array<QRgb, 256> lut{};
    for (int v = 0; v < 256; ++v)
    {
        int rr, gg, bb;
        if (v < 128)
        {
            rr = 0;
            gg = v * 2;
            bb = 255 - v * 2;
        }
        else
        {
            rr = (v - 128) * 2;
            gg = 255 - (v - 128) * 2;
            bb = 0;
        }
        lut[v] = qRgb(std::clamp(rr, 0, 255), std::clamp(gg, 0, 255), std::clamp(bb, 0, 255));
    }
    return lut;
}();

QImage heatMapQ(const QImage &gray, const QRect &r)
{
    QImage out(r.width(), r.height(), QImage::Format_RGB32);
    if (gray.isNull() || r.width() <= 0 || r.height() <= 0)
        return out;
    const int x0 = std::max(0, r.x());
    const int y0 = std::max(0, r.y());
    const int x1 = std::min(gray.width(), r.x() + r.width());
    const int y1 = std::min(gray.height(), r.y() + r.height());
    const int copyW = std::max(0, x1 - x0);

    const bool isGrayscale8 = (gray.format() == QImage::Format_Grayscale8);

    for (int y = 0; y < out.height(); ++y)
    {
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        const int sy = y0 + y;
        if (sy < y1 && isGrayscale8)
        {
            const uchar *sline = gray.constScanLine(sy) + x0;
            for (int x = 0; x < copyW; ++x)
            {
                dst[x] = s_heatLut[sline[x]];
            }
            for (int x = copyW; x < out.width(); ++x)
            {
                dst[x] = s_heatLut[0];
            }
        }
        else
        {
            for (int x = 0; x < out.width(); ++x)
            {
                const int sx = x0 + x;
                const int v = (sx < x1 && sy < y1) ? qRed(gray.pixel(sx, sy)) : 0;
                dst[x] = s_heatLut[static_cast<uint8_t>(v)];
            }
        }
    }
    return out;
}

} // namespace

// ─── SoftwareRenderer ────────────────────────────────────────────────────────

ImageData SoftwareRenderer::heatMap(const ImageData &gray, const RenderRect &rect) const
{
    if (gray.isNull())
        return ImageData();
    const QImage g = mvcore::toQImage(gray).convertToFormat(QImage::Format_Grayscale8);
    RenderRect r = rect;
    if (!r.isValid())
        r = {0, 0, g.width(), g.height()};
    const QRect qr(r.x, r.y, r.width, r.height);
    return mvcore::fromQImage(heatMapQ(g, qr));
}

ImageData SoftwareRenderer::scale(const ImageData &src, const RenderSize &target, RenderInterp mode)
{
    if (src.isNull() || !target.isValid())
        return ImageData();
    // M28 P1-03: use a non-owning view when the byte order matches, avoiding
    // the full-image copy for the dominant RGB24/Grayscale8/BGRA32 formats.
    QImage qsrc = mvcore::toQImageRef(src);
    if (qsrc.isNull())
        qsrc = mvcore::toQImage(src);
    const QImage q = scaleQ(qsrc, QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

ImageData SoftwareRenderer::overlayDifference(const ImageData &base, const ImageData &diff,
                                              double alpha) const
{
    if (base.isNull() || diff.isNull())
        return ImageData();
    if (alpha <= 0.0)
        return base;

    QImage bb = mvcore::toQImage(base).convertToFormat(QImage::Format_RGB32);
    QImage dd = mvcore::toQImage(diff).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(bb.width(), dd.width());
    const int h = std::min(bb.height(), dd.height());
    QImage out = bb;
    const double a = std::clamp(alpha, 0.0, 1.0);
    const int iAlpha = static_cast<int>(std::round(a * 256.0));
    const int invAlpha = 256 - iAlpha;
    for (int y = 0; y < h; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const QRgb *dline = reinterpret_cast<const QRgb *>(dd.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dv = qRed(dline[x]);
            const QRgb pix = line[x];
            const int r = (qRed(pix) * invAlpha + dv * iAlpha) >> 8;
            const int g = (qGreen(pix) * invAlpha + dv * iAlpha) >> 8;
            const int b = (qBlue(pix) * invAlpha + dv * iAlpha) >> 8;
            line[x] = qRgb(r, g, b);
        }
    }
    return mvcore::fromQImage(out);
}

ImageData SoftwareRenderer::scaleRegion(const ImageData &src, const RenderRect &region,
                                        const RenderSize &target, RenderInterp mode)
{
    if (src.isNull() || !region.isValid())
        return ImageData();
    // M28 P1-03: the OLD path converted the ENTIRE source to QImage before
    // copying the region, so every missing viewer tile paid O(full image) on
    // the UI thread. With the non-owning alias the region copy is O(region):
    // an 8K/100MP image is now rasterized a tile at a time as designed.
    QImage full = mvcore::toQImageRef(src);
    if (full.isNull())
        full = mvcore::toQImage(src); // fallback for RGBA32 (byte order swap)
    const QImage sub = full.copy(QRect(region.x, region.y, region.width, region.height));
    const QImage q = scaleQ(sub, QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

// ─── RenderEngine facade ─────────────────────────────────────────────────────

RenderEngine::RenderEngine() : m_backend(std::make_unique<SoftwareRenderer>())
{
}

RenderEngine &RenderEngine::instance()
{
    static RenderEngine inst;
    return inst;
}

void RenderEngine::setBackend(std::unique_ptr<Renderer> r)
{
    m_backend = r ? std::move(r) : std::make_unique<SoftwareRenderer>();
}

std::string RenderEngine::backendName() const
{
    return m_backend ? m_backend->backendName() : "none";
}

ImageData RenderEngine::scale(const ImageData &src, const RenderSize &target, RenderInterp mode)
{
    return m_backend->scale(src, target, mode);
}

ImageData RenderEngine::overlayDifference(const ImageData &base, const ImageData &diff,
                                          double alpha) const
{
    return m_backend->overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::heatMap(const ImageData &gray, const RenderRect &rect) const
{
    return m_backend->heatMap(gray, rect);
}

ImageData RenderEngine::scaleRegion(const ImageData &src, const RenderRect &region,
                                    const RenderSize &target, RenderInterp mode)
{
    return m_backend->scaleRegion(src, region, target, mode);
}

ImageData RenderEngine::scaleStatic(const ImageData &src, const RenderSize &target,
                                    RenderInterp mode)
{
    return instance().scale(src, target, mode);
}

ImageData RenderEngine::scaleBoundedStatic(const ImageData &src, const RenderSize &target)
{
    if (src.isNull() || !target.isValid())
        return ImageData();

    ImageData out = makeImageData(target.width, target.height, PixelFormat::RGB24);
    ImageBuffer dst = out.view();

    struct BoundedX
    {
        int sx0;
        int sx1;
        int fx;
        int invFx;
    };

    std::vector<BoundedX> xTab(static_cast<size_t>(target.width));
    const double rx = static_cast<double>(src.width) / static_cast<double>(target.width);
    const double ry = static_cast<double>(src.height) / static_cast<double>(target.height);

    for (int x = 0; x < target.width; ++x)
    {
        const double sx = (static_cast<double>(x) + 0.5) * rx - 0.5;
        const int sx0 = std::clamp(static_cast<int>(std::floor(sx)), 0, src.width - 1);
        const int sx1 = std::min(src.width - 1, sx0 + 1);
        const double fxD = std::clamp(sx - std::floor(sx), 0.0, 1.0);
        const int fx = static_cast<int>(std::round(fxD * 256.0));
        xTab[static_cast<size_t>(x)] = {sx0, sx1, fx, 256 - fx};
    }

    for (int y = 0; y < target.height; ++y)
    {
        const double sy = (static_cast<double>(y) + 0.5) * ry - 0.5;
        const int sy0 = std::clamp(static_cast<int>(std::floor(sy)), 0, src.height - 1);
        const int sy1 = std::min(src.height - 1, sy0 + 1);
        const double fyD = std::clamp(sy - std::floor(sy), 0.0, 1.0);
        const int fy = static_cast<int>(std::round(fyD * 256.0));
        const int w0y = 256 - fy;
        const int w1y = fy;

        uint8_t *line = dst.data + static_cast<size_t>(y) * dst.stride();
        for (int x = 0; x < target.width; ++x)
        {
            const auto &tab = xTab[static_cast<size_t>(x)];
            const int w00 = (tab.invFx * w0y) >> 8;
            const int w10 = (tab.fx * w0y) >> 8;
            const int w01 = (tab.invFx * w1y) >> 8;
            const int w11 = (tab.fx * w1y) >> 8;

            const PixelRGBA p00 = samplePixel(src, tab.sx0, sy0);
            const PixelRGBA p10 = samplePixel(src, tab.sx1, sy0);
            const PixelRGBA p01 = samplePixel(src, tab.sx0, sy1);
            const PixelRGBA p11 = samplePixel(src, tab.sx1, sy1);

            uint8_t *pixel = line + static_cast<size_t>(x) * 3;
            const int r = (w00 * p00.r + w10 * p10.r + w01 * p01.r + w11 * p11.r + 128) >> 8;
            const int g = (w00 * p00.g + w10 * p10.g + w01 * p01.g + w11 * p11.g + 128) >> 8;
            const int b = (w00 * p00.b + w10 * p10.b + w01 * p01.b + w11 * p11.b + 128) >> 8;
            pixel[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            pixel[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            pixel[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
        }
    }
    return out;
}

ImageData RenderEngine::overlayDifferenceStatic(const ImageData &base, const ImageData &diff,
                                                double alpha)
{
    return instance().overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::scaleRegionStatic(const ImageData &src, const RenderRect &region,
                                          const RenderSize &target, RenderInterp mode)
{
    return instance().scaleRegion(src, region, target, mode);
}

// ─── RenderCommand pipeline (ImageData-based) ────────────────────────────────

ImageData RenderEngine::executeCommand(const RenderCommand &cmd) const
{
    switch (cmd.type)
    {
    case RenderCommandType::DrawImage:
        return cmd.srcImage;
    case RenderCommandType::DrawOverlay:
        return cmd.overlayImage;
    case RenderCommandType::DrawHeatmap:
    {
        if (cmd.srcImage.isNull())
            return ImageData();
        return m_backend->heatMap(cmd.srcImage, cmd.rect);
    }
    case RenderCommandType::DrawHistogram:
    case RenderCommandType::DrawSelection:
    case RenderCommandType::DrawPixelMarker:
        return ImageData();
    }
    return ImageData();
}

ImageData RenderEngine::executeCommand(const RenderCommand &cmd, const ImageData &buffer) const
{
    ImageData produced = executeCommand(cmd);
    switch (cmd.type)
    {
    case RenderCommandType::DrawImage:
    case RenderCommandType::DrawHeatmap:
        return produced;
    case RenderCommandType::DrawOverlay:
    {
        if (produced.isNull())
            return buffer;
        return m_backend->overlayDifference(buffer, produced, cmd.alpha);
    }
    case RenderCommandType::DrawHistogram:
    case RenderCommandType::DrawSelection:
    case RenderCommandType::DrawPixelMarker:
        return buffer;
    }
    return buffer;
}

ImageData RenderEngine::executeCommands(const std::vector<RenderCommand> &cmds) const
{
    ImageData buffer;
    for (const auto &cmd : cmds)
        buffer = executeCommand(cmd, buffer);
    return buffer;
}

// ─── RenderCommand pipeline (QPainter-based) ─────────────────────────────────

void RenderEngine::executeCommand(QPainter &painter, const RenderCommand &cmd,
                                  const QRect &viewport)
{
    MV_TRACE_SCOPED("RenderEngine::executeCommand");
    switch (cmd.type)
    {
    case RenderCommandType::DrawImage:
        executeDrawImage(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawOverlay:
        executeDrawOverlay(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawSelection:
        executeDrawSelection(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawHistogram:
        executeDrawHistogram(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawHeatmap:
        executeDrawHeatmap(painter, cmd, viewport);
        break;
    default:
        break;
    }
}

void RenderEngine::executeDrawImage(QPainter &painter, const RenderCommand &cmd,
                                    const QRect &viewport)
{
    if (cmd.srcImage.isNull() || !cmd.rect.isValid())
        return;
    RenderSize tgt = cmd.targetSize;
    if (!tgt.isValid())
        tgt = {cmd.rect.width, cmd.rect.height};
    const RenderInterp mode = static_cast<RenderInterp>(std::clamp(cmd.interp, 0, 3));
    ImageData scaled = m_backend->scale(cmd.srcImage, tgt, mode);
    QImage q = mvcore::toQImage(scaled);
    if (q.isNull())
        return;
    painter.save();
    painter.setClipRect(viewport);
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}

void RenderEngine::executeDrawOverlay(QPainter &painter, const RenderCommand &cmd,
                                      const QRect &viewport)
{
    if (cmd.overlayImage.isNull() || !cmd.rect.isValid())
        return;
    RenderSize tgt = cmd.targetSize;
    if (!tgt.isValid())
        tgt = {cmd.rect.width, cmd.rect.height};
    const RenderInterp mode = static_cast<RenderInterp>(std::clamp(cmd.interp, 0, 3));
    ImageData scaled = m_backend->scale(cmd.overlayImage, tgt, mode);
    QImage q = mvcore::toQImage(scaled);
    if (q.isNull())
        return;
    painter.save();
    painter.setClipRect(viewport);
    painter.setOpacity(std::clamp(cmd.alpha, 0.0, 1.0));
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}

void RenderEngine::executeDrawSelection(QPainter &painter, const RenderCommand &cmd,
                                        const QRect &viewport)
{
    if (!cmd.rect.isValid())
        return;
    painter.save();
    painter.setClipRect(viewport);
    QPen pen(QColor(static_cast<QRgb>(cmd.rgba)));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height));
    painter.restore();
}

void RenderEngine::executeDrawHistogram(QPainter &painter, const RenderCommand &cmd,
                                        const QRect &viewport)
{
    const QRect r(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height);
    if (!r.isValid() || cmd.histCount <= 0)
        return;
    painter.save();
    painter.setClipRect(viewport);
    // 小黑底
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRect(r);
    int maxV = 1;
    const int n = std::min(cmd.histCount, 256);
    for (int i = 0; i < n; ++i)
        if (cmd.histData[i] > maxV)
            maxV = cmd.histData[i];
    // 白色半透明折线
    painter.setPen(QColor(255, 255, 255, 180));
    painter.setBrush(Qt::NoBrush);
    QPointF prev;
    for (int i = 0; i < n; ++i)
    {
        const double px = r.x() + static_cast<double>(i) / 255 * (r.width() - 1);
        const double py =
            r.y() + r.height() - static_cast<double>(cmd.histData[i]) / maxV * (r.height() - 1);
        const QPointF cur(px, py);
        if (i > 0)
            painter.drawLine(prev, cur);
        prev = cur;
    }
    painter.restore();
}

void RenderEngine::executeDrawHeatmap(QPainter &painter, const RenderCommand &cmd,
                                      const QRect &viewport)
{
    QImage q = mvcore::toQImage(cmd.srcImage);
    if (q.isNull() || !cmd.rect.isValid())
        return;
    painter.save();
    painter.setClipRect(viewport);
    if (cmd.interp != 0)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}
