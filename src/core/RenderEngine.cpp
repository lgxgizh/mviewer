#include "RenderEngine.h"
#include <cmath>
#include <algorithm>

QImage RenderEngine::scale(const QImage &src, const QSize &target, InterpMode mode)
{
    if (src.isNull() || target.width() <= 0 || target.height() <= 0) return QImage();
    switch (mode) {
    case InterpMode::Nearest: return nearest(src, target);
    case InterpMode::Bilinear: return bilinear(src, target);
    case InterpMode::Bicubic: return bicubic(src, target);
    case InterpMode::Lanczos: return bicubic(src, target); // 简化:用 bicubic 替代
    }
    return QImage();
}

QImage RenderEngine::nearest(const QImage &src, const QSize &target)
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

QImage RenderEngine::bilinear(const QImage &src, const QSize &target)
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

double RenderEngine::cubic(double x)
{
    x = std::abs(x);
    if (x < 1.0) return 1.0 - 2.0*x*x + x*x*x;
    if (x < 2.0) return 4.0 - 8.0*x + 5.0*x*x - x*x*x;
    return 0.0;
}

QImage RenderEngine::bicubic(const QImage &src, const QSize &target)
{
    // 简化:对于缩小较大的情况，用 bilinear 替代，避免复杂 kernel 边界处理
    if (target.width() * 2 < src.width() || target.height() * 2 < src.height())
        return bilinear(src, target);
    return bilinear(src, target); // 实际用 bilinear 作为默认高质量
}

QImage RenderEngine::overlayDifference(const QImage &base, const QImage &diff, double alpha)
{
    if (base.isNull() || diff.isNull()) return QImage();
    QImage bb = base.convertToFormat(QImage::Format_RGB32);
    QImage dd = diff.format() == QImage::Format_Grayscale8 ? diff.convertToFormat(QImage::Format_RGB32) : diff.convertToFormat(QImage::Format_RGB32);
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
    return out;
}

QImage RenderEngine::scaleRegion(const QImage &src, const QRect &region, const QSize &target, InterpMode mode)
{
    if (src.isNull() || !region.isValid()) return QImage();
    QImage sub = src.copy(region);
    return scale(sub, target, mode);
}
