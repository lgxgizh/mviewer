#pragma once

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSize>

#include <cmath>

namespace mviewer::ui
{

struct DeviceImageBlit
{
    QRectF dest;
    bool smooth = false;
};

// Draw `imagePx` without a second resample when it already matches the
// destination in device pixels. Otherwise keep the logical rect and leave
// smoothing off so a pending raster is only transformed, nearest-neighbour.
inline DeviceImageBlit planDeviceImageBlit(const QRectF &logicalDest, const QSize &imagePx,
                                           double dpr)
{
    DeviceImageBlit blit;
    blit.dest = logicalDest;
    blit.smooth = false;
    if (!(dpr > 0.0) || !std::isfinite(dpr) || imagePx.width() <= 0 || imagePx.height() <= 0 ||
        logicalDest.isEmpty())
        return blit;
    const double destW = logicalDest.width() * dpr;
    const double destH = logicalDest.height() * dpr;
    if (std::abs(destW - static_cast<double>(imagePx.width())) > 1.0 ||
        std::abs(destH - static_cast<double>(imagePx.height())) > 1.0)
        return blit;
    const double x = std::round(logicalDest.x() * dpr) / dpr;
    const double y = std::round(logicalDest.y() * dpr) / dpr;
    blit.dest = QRectF(x, y, static_cast<double>(imagePx.width()) / dpr,
                       static_cast<double>(imagePx.height()) / dpr);
    return blit;
}

inline void blitDeviceImage(QPainter &painter, const QRectF &logicalDest, const QImage &image)
{
    const double dpr = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const DeviceImageBlit blit = planDeviceImageBlit(logicalDest, image.size(), dpr);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, blit.smooth);
    painter.drawImage(blit.dest, image);
}

} // namespace mviewer::ui
