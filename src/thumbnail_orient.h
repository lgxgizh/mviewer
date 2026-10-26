#pragma once

// EXIF orientation 1-8 applied to a thumbnail raster. Pixel-exact with
// mviewer::core::displayToRaw (see test_pixelinspector). Uses Qt transforms
// instead of a per-pixel loop (~25× faster at 256 px).

#include <QImage>
#include <QTransform>

#include <algorithm>
#include <cmath>

inline QImage orientThumbnailImage(const QImage &thumb, int orientation)
{
    if (thumb.isNull() || orientation <= 1 || orientation > 8)
        return thumb;
    const QImage src = thumb.convertToFormat(QImage::Format_ARGB32);
    switch (orientation)
    {
    case 2:
        return src.mirrored(true, false);
    case 3:
        return src.transformed(QTransform().rotate(180));
    case 4:
        return src.mirrored(false, true);
    case 5:
        return src.mirrored(true, false).transformed(QTransform().rotate(-90));
    case 6:
        return src.transformed(QTransform().rotate(90));
    case 7:
        return src.mirrored(true, false).transformed(QTransform().rotate(90));
    case 8:
        return src.transformed(QTransform().rotate(-90));
    default:
        return src;
    }
}

// Embedded EXIF thumbnails are often a fixed 4:3 raster (160x120) with black
// bars baked in for 3:2 / 16:9 sources. Center-crop such a thumb to the
// source's display aspect so every consumer (gallery cell, selection preview,
// viewer placeholder) sees the photo's real shape instead of a smaller photo
// inside a wrong-aspect frame. Returns the input unchanged when the display
// size is unknown, the aspects already agree (< 1%), or the crop would drop
// more than 30% of an axis (a sideways or unrelated thumb is left alone).
inline QImage cropThumbnailToAspect(const QImage &thumb, int displayW, int displayH)
{
    if (thumb.isNull() || thumb.width() <= 0 || thumb.height() <= 0 || displayW <= 0 ||
        displayH <= 0)
        return thumb;
    const double target = static_cast<double>(displayW) / static_cast<double>(displayH);
    const double have = static_cast<double>(thumb.width()) / static_cast<double>(thumb.height());
    const double ratio = have / target;
    if (std::abs(ratio - 1.0) < 0.01)
        return thumb;
    if (ratio > 1.0)
    {
        const int w = std::max(1, static_cast<int>(std::lround(thumb.height() * target)));
        if (w >= thumb.width() || w < thumb.width() * 0.7)
            return thumb;
        return thumb.copy((thumb.width() - w) / 2, 0, w, thumb.height());
    }
    const int h = std::max(1, static_cast<int>(std::lround(thumb.width() / target)));
    if (h >= thumb.height() || h < thumb.height() * 0.7)
        return thumb;
    return thumb.copy(0, (thumb.height() - h) / 2, thumb.width(), h);
}
