#pragma once

// EXIF orientation 1-8 applied to a thumbnail raster. Pixel-exact with
// mviewer::core::displayToRaw (see test_pixelinspector). Uses Qt transforms
// instead of a per-pixel loop (~25× faster at 256 px).

#include <QImage>
#include <QTransform>

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
