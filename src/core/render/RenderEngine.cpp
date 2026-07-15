#include "core/render/RenderEngine.h"

#include "core/image/QtConvert.h"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace
{

QImage nearestQ(const QImage& src, const QSize& target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    for (int y = 0; y < th; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const int sy = std::min(sh - 1, (y * sh) / th);
        const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(sy));
        for (int x = 0; x < tw; ++x)
        {
            const int sx = std::min(sw - 1, (x * sw) / tw);
            line[x] = sline[sx];
        }
    }
    return out;
}

QImage bilinearQ(const QImage& src, const QSize& target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = std::max(0, std::min(sh - 2, static_cast<int>(std::floor(sy))));
        const double fy = std::max(0.0, sy - std::floor(sy));
        const QRgb* sl0 = reinterpret_cast<const QRgb*>(src.constScanLine(y0));
        const QRgb* sl1 = reinterpret_cast<const QRgb*>(src.constScanLine(y0 + 1));
        for (int x = 0; x < tw; ++x)
        {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = std::max(0, std::min(sw - 2, static_cast<int>(std::floor(sx))));
            const double fx = std::max(0.0, sx - std::floor(sx));
            double r = (1 - fx) * (1 - fy) * qRed(sl0[x0]) + fx * (1 - fy) * qRed(sl0[x0 + 1]) +
                       (1 - fx) * fy * qRed(sl1[x0]) + fx * fy * qRed(sl1[x0 + 1]);
            double g = (1 - fx) * (1 - fy) * qGreen(sl0[x0]) + fx * (1 - fy) * qGreen(sl0[x0 + 1]) +
                       (1 - fx) * fy * qGreen(sl1[x0]) + fx * fy * qGreen(sl1[x0 + 1]);
            double b = (1 - fx) * (1 - fy) * qBlue(sl0[x0]) + fx * (1 - fy) * qBlue(sl0[x0 + 1]) +
                       (1 - fx) * fy * qBlue(sl1[x0]) + fx * fy * qBlue(sl1[x0 + 1]);
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

QImage bicubicQ(const QImage& src, const QSize& target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        for (int x = 0; x < tw; ++x)
        {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = -1; m <= 2; ++m)
            {
                const int syy = std::max(0, std::min(sh - 1, y0 + m));
                const double wy = cubicKernel(sy - (y0 + m));
                const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(syy));
                for (int n = -1; n <= 2; ++n)
                {
                    const int sxx = std::max(0, std::min(sw - 1, x0 + n));
                    const double wx = cubicKernel(sx - (x0 + n));
                    const double w = wx * wy;
                    r += w * qRed(sline[sxx]);
                    g += w * qGreen(sline[sxx]);
                    b += w * qBlue(sline[sxx]);
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

QImage lanczosQ(const QImage& src, const QSize& target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        for (int x = 0; x < tw; ++x)
        {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = -2; m <= 2; ++m)
            {
                const int syy = std::max(0, std::min(sh - 1, y0 + m));
                const double wy = lanczosKernel(sy - (y0 + m));
                const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(syy));
                for (int n = -2; n <= 2; ++n)
                {
                    const int sxx = std::max(0, std::min(sw - 1, x0 + n));
                    const double wx = lanczosKernel(sx - (x0 + n));
                    const double w = wx * wy;
                    r += w * qRed(sline[sxx]);
                    g += w * qGreen(sline[sxx]);
                    b += w * qBlue(sline[sxx]);
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

QImage scaleQ(const QImage& src, const QSize& target, RenderInterp mode)
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

QImage heatMapQ(const QImage& gray, const QRect& r)
{
    QImage out(r.width(), r.height(), QImage::Format_RGB32);
    if (gray.isNull() || r.width() <= 0 || r.height() <= 0)
        return out;
    const int x0 = std::max(0, r.x());
    const int y0 = std::max(0, r.y());
    const int x1 = std::min(gray.width(), r.x() + r.width());
    const int y1 = std::min(gray.height(), r.y() + r.height());
    for (int y = 0; y < out.height(); ++y)
    {
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            const int sx = x0 + x;
            const int sy = y0 + y;
            const int v = (sx < x1 && sy < y1)
                ? qRed(gray.pixel(sx, sy))
                : 0;
            // Blue (cold) -> Green -> Red (hot)
            int rr, gg, bb;
            if (v < 128) {
                rr = 0;
                gg = v * 2;
                bb = 255 - v * 2;
            } else {
                rr = (v - 128) * 2;
                gg = 255 - (v - 128) * 2;
                bb = 0;
            }
            dst[x] = qRgb(std::clamp(rr, 0, 255), std::clamp(gg, 0, 255), std::clamp(bb, 0, 255));
        }
    }
    return out;
}

} // namespace

// ─── SoftwareRenderer ────────────────────────────────────────────────────────

ImageData SoftwareRenderer::heatMap(const ImageData& gray, const RenderRect& rect) const
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

ImageData SoftwareRenderer::scale(const ImageData& src, const RenderSize& target, RenderInterp mode)
{
    if (src.isNull() || !target.isValid())
        return ImageData();
    const QImage q = scaleQ(mvcore::toQImage(src), QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

ImageData
SoftwareRenderer::overlayDifference(const ImageData& base, const ImageData& diff, double alpha) const
{
    if (base.isNull() || diff.isNull())
        return ImageData();
    QImage bb = mvcore::toQImage(base).convertToFormat(QImage::Format_RGB32);
    QImage dd = mvcore::toQImage(diff).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(bb.width(), dd.width());
    const int h = std::min(bb.height(), dd.height());
    QImage out = bb;
    const double a = std::clamp(alpha, 0.0, 1.0);
    for (int y = 0; y < h; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const QRgb* dline = reinterpret_cast<const QRgb*>(dd.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dv = qRed(dline[x]);
            const int r = static_cast<int>(qRed(line[x]) * (1.0 - a) + dv * a);
            const int g = static_cast<int>(qGreen(line[x]) * (1.0 - a) + dv * a);
            const int b = static_cast<int>(qBlue(line[x]) * (1.0 - a) + dv * a);
            line[x] = qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }
    return mvcore::fromQImage(out);
}

ImageData SoftwareRenderer::scaleRegion(const ImageData& src,
    const RenderRect& region,
    const RenderSize& target,
    RenderInterp mode)
{
    if (src.isNull() || !region.isValid())
        return ImageData();
    const QImage full = mvcore::toQImage(src);
    const QImage sub = full.copy(QRect(region.x, region.y, region.width, region.height));
    const QImage q = scaleQ(sub, QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

// ─── RenderEngine facade ─────────────────────────────────────────────────────

RenderEngine::RenderEngine()
    : m_backend(std::make_unique<SoftwareRenderer>())
{
}

RenderEngine& RenderEngine::instance()
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

ImageData RenderEngine::scale(const ImageData& src, const RenderSize& target, RenderInterp mode)
{
    return m_backend->scale(src, target, mode);
}

ImageData
RenderEngine::overlayDifference(const ImageData& base, const ImageData& diff, double alpha) const
{
    return m_backend->overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::heatMap(const ImageData& gray, const RenderRect& rect) const
{
    return m_backend->heatMap(gray, rect);
}

ImageData
RenderEngine::scaleRegion(const ImageData& src, const RenderRect& region,
    const RenderSize& target, RenderInterp mode)
{
    return m_backend->scaleRegion(src, region, target, mode);
}

ImageData
RenderEngine::scaleStatic(const ImageData& src, const RenderSize& target, RenderInterp mode)
{
    return instance().scale(src, target, mode);
}

ImageData
RenderEngine::overlayDifferenceStatic(const ImageData& base, const ImageData& diff, double alpha)
{
    return instance().overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::scaleRegionStatic(const ImageData& src,
    const RenderRect& region,
    const RenderSize& target,
    RenderInterp mode)
{
    return instance().scaleRegion(src, region, target, mode);
}

// ─── RenderCommand pipeline (ImageData-based) ────────────────────────────────

ImageData RenderEngine::executeCommand(const RenderCommand& cmd) const
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

ImageData RenderEngine::executeCommand(const RenderCommand& cmd, const ImageData& buffer) const
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

ImageData RenderEngine::executeCommands(const std::vector<RenderCommand>& cmds) const
{
    ImageData buffer;
    for (const auto& cmd : cmds)
        buffer = executeCommand(cmd, buffer);
    return buffer;
}

// ─── RenderCommand pipeline (QPainter-based) ─────────────────────────────────

void RenderEngine::executeCommand(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
{
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

void RenderEngine::executeDrawImage(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
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

void RenderEngine::executeDrawOverlay(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
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

void RenderEngine::executeDrawSelection(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
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

void RenderEngine::executeDrawHistogram(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
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
        const double py = r.y() + r.height() - static_cast<double>(cmd.histData[i]) / maxV * (r.height() - 1);
        const QPointF cur(px, py);
        if (i > 0)
            painter.drawLine(prev, cur);
        prev = cur;
    }
    painter.restore();
}

void RenderEngine::executeDrawHeatmap(QPainter& painter,
    const RenderCommand& cmd,
    const QRect& viewport)
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
