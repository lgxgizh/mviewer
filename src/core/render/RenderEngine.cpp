#include "core/render/RenderEngine.h"
#include "core/image/QtConvert.h"

#include <QImage>
#include <QSize>
#include <QRect>
#include <cmath>
#include <algorithm>

// 内部实现：把 ImageData 转成 QImage 做像素级插值，输出再转回 ImageData。
// header 不暴露 Qt；这里在 .cpp 内部使用 Qt 作为实现细节。
namespace {

QImage nearestQ(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    for (int y = 0; y < th; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const int sy = std::min(sh - 1, (y * sh) / th);
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        for (int x = 0; x < tw; ++x) {
            const int sx = std::min(sw - 1, (x * sw) / tw);
            line[x] = sline[sx];
        }
    }
    return out;
}

QImage bilinearQ(const QImage &src, const QSize &target)
{
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;

    for (int y = 0; y < th; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = std::max(0, std::min(sh - 2, static_cast<int>(std::floor(sy))));
        const double fy = std::max(0.0, sy - std::floor(sy));
        const QRgb *sl0 = reinterpret_cast<const QRgb *>(src.constScanLine(y0));
        const QRgb *sl1 = reinterpret_cast<const QRgb *>(src.constScanLine(y0 + 1));
        for (int x = 0; x < tw; ++x) {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = std::max(0, std::min(sw - 2, static_cast<int>(std::floor(sx))));
            const double fx = std::max(0.0, sx - std::floor(sx));

            double r = (1-fx)*(1-fy)*qRed(sl0[x0]) + fx*(1-fy)*qRed(sl0[x0+1]) +
                       (1-fx)*fy*qRed(sl1[x0]) + fx*fy*qRed(sl1[x0+1]);
            double g = (1-fx)*(1-fy)*qGreen(sl0[x0]) + fx*(1-fy)*qGreen(sl0[x0+1]) +
                       (1-fx)*fy*qGreen(sl1[x0]) + fx*fy*qGreen(sl1[x0+1]);
            double b = (1-fx)*(1-fy)*qBlue(sl0[x0]) + fx*(1-fy)*qBlue(sl0[x0+1]) +
                       (1-fx)*fy*qBlue(sl1[x0]) + fx*fy*qBlue(sl1[x0+1]);
            line[x] = qRgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
        }
    }
    return out;
}

QImage bicubicQ(const QImage &src, const QSize &target)
{
    // 简化：用 bilinear 作为高质量默认（保留原实现语义）
    return bilinearQ(src, target);
}

QImage scaleQ(const QImage &src, const QSize &target, InterpMode mode)
{
    if (src.isNull() || target.width() <= 0 || target.height() <= 0) return QImage();
    const QImage rgb = src.convertToFormat(QImage::Format_RGB32);
    switch (mode) {
    case InterpMode::Nearest: return nearestQ(rgb, target);
    case InterpMode::Bilinear: return bilinearQ(rgb, target);
    case InterpMode::Bicubic: return bicubicQ(rgb, target);
    case InterpMode::Lanczos: return bicubicQ(rgb, target);
    }
    return QImage();
}

} // namespace

ImageData RenderEngine::scale(const ImageData &src, const RenderSize &target,
                              InterpMode mode)
{
    if (src.isNull() || target.width <= 0 || target.height <= 0)
        return ImageData();
    const QImage q = scaleQ(mvcore::toQImage(src),
                            QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

ImageData RenderEngine::overlayDifference(const ImageData &base,
                                          const ImageData &diff, double alpha)
{
    if (base.isNull() || diff.isNull())
        return ImageData();
    QImage bb = mvcore::toQImage(base).convertToFormat(QImage::Format_RGB32);
    QImage dd = mvcore::toQImage(diff).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(bb.width(), dd.width());
    const int h = std::min(bb.height(), dd.height());
    QImage out = bb;
    const double a = std::clamp(alpha, 0.0, 1.0);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const QRgb *dline = reinterpret_cast<const QRgb *>(dd.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int dv = qRed(dline[x]); // diff 是灰度，用 R 分量
            const int r = static_cast<int>(qRed(line[x]) * (1.0 - a) + dv * a);
            const int g = static_cast<int>(qGreen(line[x]) * (1.0 - a) + dv * a);
            const int b = static_cast<int>(qBlue(line[x]) * (1.0 - a) + dv * a);
            line[x] = qRgb(std::clamp(r,0,255), std::clamp(g,0,255), std::clamp(b,0,255));
        }
    }
    return mvcore::fromQImage(out);
}

ImageData RenderEngine::scaleRegion(const ImageData &src, const RenderRect &region,
                                    const RenderSize &target, InterpMode mode)
{
    if (src.isNull() || !region.isValid())
        return ImageData();
    const QImage full = mvcore::toQImage(src);
    const QImage sub = full.copy(QRect(region.x, region.y, region.width, region.height));
    const QImage q = scaleQ(sub, QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}
