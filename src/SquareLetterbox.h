#pragma once

#include <QImage>

#include <algorithm>

namespace mviewer::ui
{

inline bool sameLetterboxColor(QRgb a, QRgb b)
{
    if (qAlpha(a) == 0 && qAlpha(b) == 0)
        return true;
    return qAlpha(a) == qAlpha(b) && std::abs(qRed(a) - qRed(b)) <= 2 &&
           std::abs(qGreen(a) - qGreen(b)) <= 2 && std::abs(qBlue(a) - qBlue(b)) <= 2;
}

// Square gallery thumbs are stored as RGB, so transparent letterbox padding
// becomes a solid mat. Padding on only one axis is that mat, not the photo.
inline QImage cropSquareLetterbox(const QImage &src)
{
    if (src.isNull() || src.width() < 8 || src.height() < 8 || src.width() != src.height())
        return src;
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    const QRgb pad = img.pixel(0, 0);
    if (!sameLetterboxColor(pad, img.pixel(w - 1, 0)) ||
        !sameLetterboxColor(pad, img.pixel(0, h - 1)) ||
        !sameLetterboxColor(pad, img.pixel(w - 1, h - 1)))
        return src;

    auto rowIsPad = [&](int y)
    {
        const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            if (!sameLetterboxColor(row[x], pad))
                return false;
        }
        return true;
    };
    auto colIsPad = [&](int x)
    {
        for (int y = 0; y < h; ++y)
        {
            const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            if (!sameLetterboxColor(row[x], pad))
                return false;
        }
        return true;
    };

    int top = 0;
    while (top < h && rowIsPad(top))
        ++top;
    int bottom = h - 1;
    while (bottom >= top && rowIsPad(bottom))
        --bottom;
    int left = 0;
    while (left < w && colIsPad(left))
        ++left;
    int right = w - 1;
    while (right >= left && colIsPad(right))
        --right;
    if (left >= right || top >= bottom)
        return src;

    const bool verticalPad = top >= 4 && (h - 1 - bottom) >= 4;
    const bool horizontalPad = left >= 4 && (w - 1 - right) >= 4;
    // A frame has padding on both axes. Letterboxing is one axis only.
    if (verticalPad == horizontalPad)
        return src;
    return img.copy(left, top, right - left + 1, bottom - top + 1);
}

inline QImage photoFromSquareThumb(const QImage &src)
{
    if (src.isNull() || src.width() != src.height())
        return src;
    const QImage cropped = cropSquareLetterbox(src);
    if (cropped.isNull() || cropped.size() == src.size())
        return src;
    return cropped;
}

} // namespace mviewer::ui
