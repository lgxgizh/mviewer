#pragma once

// One chip style for everything drawn on top of an image (filename, zoom,
// ROI size, pane header, ROI stats): black at 55 % opacity, 6 px radius,
// the same padding, and the theme caption size. Painted chips and QLabel chips
// share these values so a compare snapshot (Ctrl+C / Ctrl+S) reproduces what
// the panes show.

#include "ThemeMetrics.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace mviewer::ui
{

inline constexpr int kOverlayChipAlpha = 140; // 55 % of 255
inline constexpr int kOverlayChipRadius = 6;
inline constexpr int kOverlayChipPadX = 6;
inline constexpr int kOverlayChipPadY = 3;
inline constexpr int kOverlayChipFontPx = mviewer::ui::metrics::kFontCaptionPx;

inline QColor overlayChipColor()
{
    return QColor(0, 0, 0, kOverlayChipAlpha);
}

// Small chip text: caption size, semibold, on the painter's/widget's family.
inline QFont overlayChipFont(QFont base)
{
    base.setPixelSize(kOverlayChipFontPx);
    base.setWeight(QFont::DemiBold);
    return base;
}

// Stylesheet fragment for QLabel chips. rgba is written without spaces so the
// live theme retint (which matches token strings) never rewrites it.
// `border` is a CSS border value; ROI chips pass the ROI colour so each chip
// stays tied to its outline.
inline QString overlayChipStyle(const QString &selector,
                                const QString &border = QStringLiteral("none"))
{
    return QStringLiteral("%1{background:rgba(0,0,0,%2);color:#ffffff;border:%7;"
                          "border-radius:%3px;padding:%4px %5px;font-size:%6px;"
                          "font-weight:600;}")
        .arg(selector)
        .arg(kOverlayChipAlpha)
        .arg(kOverlayChipRadius)
        .arg(kOverlayChipPadY)
        .arg(kOverlayChipPadX)
        .arg(kOverlayChipFontPx)
        .arg(border);
}

inline void paintOverlayChipBackground(QPainter &p, const QRectF &box,
                                       const QColor &color = overlayChipColor())
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(box, kOverlayChipRadius, kOverlayChipRadius);
}

// Chip size for one line of text in the current painter font.
inline QSizeF overlayChipSize(const QFontMetrics &metrics, const QString &text)
{
    return QSizeF(metrics.horizontalAdvance(text) + kOverlayChipPadX * 2,
                  metrics.height() + kOverlayChipPadY * 2);
}

} // namespace mviewer::ui
