#include "core/render/RenderEngine.h"
#include "core/image/QtConvert.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {

QImage nearestQ(const QImage& src, const QSize& target) {
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    for (int y = 0; y < th; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const int sy = std::min(sh - 1, (y * sh) / th);
        const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(sy));
        for (int x = 0; x < tw; ++x) {
            const int sx = std::min(sw - 1, (x * sw) / tw);
            line[x] = sline[sx];
        }
    }
    return out;
}

QImage bilinearQ(const QImage& src, const QSize& target) {
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = std::max(0, std::min(sh - 2, static_cast<int>(std::floor(sy))));
        const double fy = std::max(0.0, sy - std::floor(sy));
        const QRgb* sl0 = reinterpret_cast<const QRgb*>(src.constScanLine(y0));
        const QRgb* sl1 = reinterpret_cast<const QRgb*>(src.constScanLine(y0 + 1));
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

// Bicubic interpolation kernel (Catmull-Rom, a=0.5)
static double cubicKernel(double x) {
    x = std::abs(x);
    if (x < 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    } else if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

QImage bicubicQ(const QImage& src, const QSize& target) {
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        for (int x = 0; x < tw; ++x) {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = -1; m <= 2; ++m) {
                const int syy = std::max(0, std::min(sh - 1, y0 + m));
                const double wy = cubicKernel(sy - (y0 + m));
                const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(syy));
                for (int n = -1; n <= 2; ++n) {
                    const int sxx = std::max(0, std::min(sw - 1, x0 + n));
                    const double wx = cubicKernel(sx - (x0 + n));
                    const double w = wx * wy;
                    r += w * qRed(sline[sxx]);
                    g += w * qGreen(sline[sxx]);
                    b += w * qBlue(sline[sxx]);
                    wsum += w;
                }
            }
            if (wsum > 0) { r /= wsum; g /= wsum; b /= wsum; }
            line[x] = qRgb(
                std::clamp(static_cast<int>(std::round(r)), 0, 255),
                std::clamp(static_cast<int>(std::round(g)), 0, 255),
                std::clamp(static_cast<int>(std::round(b)), 0, 255));
        }
    }
    return out;
}

// Lanczos kernel (3-lobe)
static constexpr double PI_CONST = 3.14159265358979323846;

static double lanczosKernel(double x) {
    x = std::abs(x);
    if (x < 1e-6) return 1.0;
    if (x >= 3.0) return 0.0;
    const double pix = PI_CONST * x;
    return 3.0 * std::sin(pix) * std::sin(pix / 3.0) / (pix * pix);
}

QImage lanczosQ(const QImage& src, const QSize& target) {
    QImage out(target, QImage::Format_RGB32);
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    const double rx = static_cast<double>(sw) / tw;
    const double ry = static_cast<double>(sh) / th;
    for (int y = 0; y < th; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double sy = (y + 0.5) * ry - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        for (int x = 0; x < tw; ++x) {
            const double sx = (x + 0.5) * rx - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            double r = 0, g = 0, b = 0, wsum = 0;
            for (int m = -2; m <= 2; ++m) {
                const int syy = std::max(0, std::min(sh - 1, y0 + m));
                const double wy = lanczosKernel(sy - (y0 + m));
                const QRgb* sline = reinterpret_cast<const QRgb*>(src.constScanLine(syy));
                for (int n = -2; n <= 2; ++n) {
                    const int sxx = std::max(0, std::min(sw - 1, x0 + n));
                    const double wx = lanczosKernel(sx - (x0 + n));
                    const double w = wx * wy;
                    r += w * qRed(sline[sxx]);
                    g += w * qGreen(sline[sxx]);
                    b += w * qBlue(sline[sxx]);
                    wsum += w;
                }
            }
            if (wsum > 0) { r /= wsum; g /= wsum; b /= wsum; }
            line[x] = qRgb(
                std::clamp(static_cast<int>(std::round(r)), 0, 255),
                std::clamp(static_cast<int>(std::round(g)), 0, 255),
                std::clamp(static_cast<int>(std::round(b)), 0, 255));
        }
    }
    return out;
}

QImage scaleQ(const QImage& src, const QSize& target, RenderInterp mode) {
    if (src.isNull() || target.width() <= 0 || target.height() <= 0) return QImage();
    const QImage rgb = src.convertToFormat(QImage::Format_RGB32);
    switch (mode) {
    case RenderInterp::Nearest:  return nearestQ(rgb, target);
    case RenderInterp::Bilinear: return bilinearQ(rgb, target);
    case RenderInterp::Bicubic:  return bicubicQ(rgb, target);
    case RenderInterp::Lanczos:  return lanczosQ(rgb, target);
    }
    return QImage();
}

} // namespace

// ─── SoftwareRenderer ────────────────────────────────────────────────────────

ImageData SoftwareRenderer::scale(const ImageData& src, const RenderSize& target,
                                  RenderInterp mode) {
    if (src.isNull() || !target.isValid()) return ImageData();
    const QImage q = scaleQ(mvcore::toQImage(src),
                            QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

ImageData SoftwareRenderer::overlayDifference(const ImageData& base,
                                              const ImageData& diff, double alpha) {
    if (base.isNull() || diff.isNull()) return ImageData();
    QImage bb = mvcore::toQImage(base).convertToFormat(QImage::Format_RGB32);
    QImage dd = mvcore::toQImage(diff).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(bb.width(), dd.width());
    const int h = std::min(bb.height(), dd.height());
    QImage out = bb;
    const double a = std::clamp(alpha, 0.0, 1.0);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const QRgb* dline = reinterpret_cast<const QRgb*>(dd.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int dv = qRed(dline[x]);
            const int r = static_cast<int>(qRed(line[x]) * (1.0 - a) + dv * a);
            const int g = static_cast<int>(qGreen(line[x]) * (1.0 - a) + dv * a);
            const int b = static_cast<int>(qBlue(line[x]) * (1.0 - a) + dv * a);
            line[x] = qRgb(std::clamp(r,0,255), std::clamp(g,0,255), std::clamp(b,0,255));
        }
    }
    return mvcore::fromQImage(out);
}

ImageData SoftwareRenderer::scaleRegion(const ImageData& src, const RenderRect& region,
                                        const RenderSize& target, RenderInterp mode) {
    if (src.isNull() || !region.isValid()) return ImageData();
    const QImage full = mvcore::toQImage(src);
    const QImage sub = full.copy(QRect(region.x, region.y, region.width, region.height));
    const QImage q = scaleQ(sub, QSize(target.width, target.height), mode);
    return mvcore::fromQImage(q);
}

// ─── RenderEngine facade ─────────────────────────────────────────────────────

RenderEngine::RenderEngine()
    : m_backend(std::make_unique<SoftwareRenderer>()) {}

RenderEngine& RenderEngine::instance() {
    static RenderEngine inst;
    return inst;
}

void RenderEngine::setBackend(std::unique_ptr<Renderer> r) {
    m_backend = r ? std::move(r) : std::make_unique<SoftwareRenderer>();
}

std::string RenderEngine::backendName() const {
    return m_backend ? m_backend->backendName() : "none";
}

ImageData RenderEngine::scale(const ImageData& src, const RenderSize& target, RenderInterp mode) {
    return m_backend->scale(src, target, mode);
}

ImageData RenderEngine::overlayDifference(const ImageData& base, const ImageData& diff, double alpha) {
    return m_backend->overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::scaleRegion(const ImageData& src, const RenderRect& region,
                                    const RenderSize& target, RenderInterp mode) {
    return m_backend->scaleRegion(src, region, target, mode);
}

ImageData RenderEngine::scaleStatic(const ImageData& src, const RenderSize& target, RenderInterp mode) {
    return instance().scale(src, target, mode);
}

ImageData RenderEngine::overlayDifferenceStatic(const ImageData& base, const ImageData& diff, double alpha) {
    return instance().overlayDifference(base, diff, alpha);
}

ImageData RenderEngine::scaleRegionStatic(const ImageData& src, const RenderRect& region,
                                           const RenderSize& target, RenderInterp mode) {
    return instance().scaleRegion(src, region, target, mode);
}
