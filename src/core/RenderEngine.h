#pragma once

#include <QImage>
#include <QSize>
#include <QRect>

// 图像缩放插值算法(独立于 QWidget)
enum class InterpMode { Nearest, Bilinear, Bicubic, Lanczos };

class RenderEngine
{
public:
    // 高质量缩放
    static QImage scale(const QImage &src, const QSize &target, InterpMode mode = InterpMode::Bilinear);

    // Difference Overlay:把差异图以指定透明度叠加到原图上
    static QImage overlayDifference(const QImage &base, const QImage &diff, double alpha = 0.5);

    // 高质量缩放(指定源区域->目标尺寸)
    static QImage scaleRegion(const QImage &src, const QRect &region, const QSize &target, InterpMode mode = InterpMode::Bilinear);

private:
    static QImage bilinear(const QImage &src, const QSize &target);
    static QImage bicubic(const QImage &src, const QSize &target);
    static QImage nearest(const QImage &src, const QSize &target);
    static inline double cubic(double x);
};
