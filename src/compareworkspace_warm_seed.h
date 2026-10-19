#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include <string>
#include <vector>

namespace mviewer::ui
{

struct CompareWarmSeed
{
    std::string path;
    QImage image;
    QSize sourceSize;
    QRect sourceRect;
};

// Geometry used while painting a soft/warm raster (Browse thumb, viewer LOD).
// When a downscaled full-frame image claims the full source size, nearest-
// neighbour blit stretches it into a mosaic. Until a sharper LOD arrives, paint
// with the real pixel size instead.
struct SoftPaintGeometry
{
    QSize sourceSize;
    QRect sourceRect;
};

inline SoftPaintGeometry softPaintGeometry(const QSize &imageSize, const QSize &claimedSourceSize,
                                           const QRect &claimedSourceRect = {})
{
    SoftPaintGeometry out;
    out.sourceSize = claimedSourceSize.isValid() ? claimedSourceSize : imageSize;
    out.sourceRect =
        claimedSourceRect.isValid() ? claimedSourceRect : QRect(QPoint(0, 0), out.sourceSize);
    if (!imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0)
        return out;
    if (!out.sourceSize.isValid() || out.sourceSize.width() <= 0 || out.sourceSize.height() <= 0)
    {
        out.sourceSize = imageSize;
        out.sourceRect = QRect(QPoint(0, 0), imageSize);
        return out;
    }
    const QRect full(QPoint(0, 0), out.sourceSize);
    if (out.sourceRect == full && (imageSize.width() < out.sourceSize.width() ||
                                   imageSize.height() < out.sourceSize.height()))
    {
        out.sourceSize = imageSize;
        out.sourceRect = QRect(QPoint(0, 0), imageSize);
    }
    return out;
}

} // namespace mviewer::ui
