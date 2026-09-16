#include "core/render/RenderEngine.h"

#include "core/image/QtConvert.h"
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
    double fx;
    double invFx;
};

QImage bilinearQ(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;

    std::vector<BilinearX> xTab(tw);
    for (int x = 0; x < tw; ++x)
    {
        const double sx = (x + 0.5) * rx - 0.5;
        const int x0 = std::max(0, std::min(sw - 2, static_cast<int>(std::floor(sx))));
        const double fx = std::max(0.0, sx - std::floor(sx));
        xTab[x] = {x0, fx, 1.0 - fx};
    }

    for (int y = 0; y < th; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = std::max(0, std::min(sh - 2, static_cast<int>(std::floor(sy))));
        const double fy = std::max(0.0, sy - std::floor(sy));
        const double w0y = 1.0 - fy;
        const double w1y = fy;
        const QRgb *sl0 = reinterpret_cast<const QRgb *>(src.constScanLine(y0));
        const QRgb *sl1 = reinterpret_cast<const QRgb *>(src.constScanLine(y0 + 1));
        for (int x = 0; x < tw; ++x)
        {
            const auto &tab = xTab[x];
            const double w00 = tab.invFx * w0y;
            const double w10 = tab.fx * w0y;
            const double w01 = tab.invFx * w1y;
            const double w11 = tab.fx * w1y;
            const int x0 = tab.x0;

            const QRgb p00 = sl0[x0];
            const QRgb p10 = sl0[x0 + 1];
            const QRgb p01 = sl1[x0];
            const QRgb p11 = sl1[x0 + 1];

            const double r =
                w00 * qRed(p00) + w10 * qRed(p10) + w01 * qRed(p01) + w11 * qRed(p11);
            const double g =
                w00 * qGreen(p00) + w10 * qGreen(p10) + w01 * qGreen(p01) + w11 * qGreen(p11);
            const double b =
                w00 * qBlue(p00) + w10 * qBlue(p10) + w01 * qBlue(p01) + w11 * qBlue(p11);
            line[x] = qRgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
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

static const std::array<QRgb, 256> s_heatLut = []() {
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
    const double rx = static_cast<double>(src.width) / target.width;
    const double ry = static_cast<double>(src.height) / target.height;
    for (int y = 0; y < target.height; ++y)
    {
        const double sy = (y + 0.5) * ry - 0.5;
        const int sy0 = std::clamp(static_cast<int>(std::floor(sy)), 0, src.height - 1);
        const int sy1 = std::min(src.height - 1, sy0 + 1);
        const double fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
        uint8_t *line = dst.data + static_cast<size_t>(y) * dst.stride();
        for (int x = 0; x < target.width; ++x)
        {
            const double sx = (x + 0.5) * rx - 0.5;
            const int sx0 = std::clamp(static_cast<int>(std::floor(sx)), 0, src.width - 1);
            const int sx1 = std::min(src.width - 1, sx0 + 1);
            const double fx = std::clamp(sx - std::floor(sx), 0.0, 1.0);
            const PixelRGBA p00 = samplePixel(src, sx0, sy0);
            const PixelRGBA p10 = samplePixel(src, sx1, sy0);
            const PixelRGBA p01 = samplePixel(src, sx0, sy1);
            const PixelRGBA p11 = samplePixel(src, sx1, sy1);
            auto lerp = [fx, fy](uint8_t a, uint8_t b, uint8_t c, uint8_t d)
            {
                const double top = a + (b - a) * fx;
                const double bottom = c + (d - c) * fx;
                return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(std::lround(top + (bottom - top) * fy)), 0, 255));
            };
            uint8_t *pixel = line + static_cast<size_t>(x) * 3;
            pixel[0] = lerp(p00.r, p10.r, p01.r, p11.r);
            pixel[1] = lerp(p00.g, p10.g, p01.g, p11.g);
            pixel[2] = lerp(p00.b, p10.b, p01.b, p11.b);
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
