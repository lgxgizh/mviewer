#pragma once

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

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

// Fit a provisional photo to the source display aspect (w:h) so it occupies
// exactly the rect the full image will: a near-match (<= 1.3x off, e.g. a 4:3
// EXIF thumb with baked-in bars for a 3:2 photo) is center-cropped to fill;
// a wilder mismatch is centered on a transparent canvas of that aspect rather
// than stretched. Unknown size or an aspect within 1% returns src unchanged.
inline QImage fillPhotoToAspect(const QImage &src, int displayW, int displayH)
{
    if (src.isNull() || displayW <= 0 || displayH <= 0 || src.width() <= 0 || src.height() <= 0)
        return src;
    const double target = static_cast<double>(displayW) / displayH;
    const double have = static_cast<double>(src.width()) / src.height();
    const double ratio = have / target;
    if (std::abs(ratio - 1.0) <= 0.01)
        return src;
    constexpr double kMaxCropRatio = 1.3;
    if (std::max(ratio, 1.0 / ratio) <= kMaxCropRatio)
    {
        if (ratio > 1.0)
        {
            const int w =
                std::clamp(static_cast<int>(std::lround(src.height() * target)), 1, src.width());
            return src.copy((src.width() - w) / 2, 0, w, src.height());
        }
        const int h =
            std::clamp(static_cast<int>(std::lround(src.width() / target)), 1, src.height());
        return src.copy(0, (src.height() - h) / 2, src.width(), h);
    }
    // Pad: canvas keeps the larger source edge so no detail is lost.
    int cw = src.width();
    int ch = src.height();
    if (ratio > 1.0)
        ch = std::max(1, static_cast<int>(std::lround(cw / target)));
    else
        cw = std::max(1, static_cast<int>(std::lround(ch * target)));
    QImage canvas(cw, ch, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter p(&canvas);
        p.drawImage((cw - src.width()) / 2, (ch - src.height()) / 2, src);
    }
    return canvas;
}

} // namespace mviewer::ui
