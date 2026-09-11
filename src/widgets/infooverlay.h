#pragma once

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>

#include <algorithm>

namespace mviewer::ui
{

inline constexpr int kFilenameOverlayPad = 6;
inline constexpr int kFilenameOverlayMaxHeight = 72;

inline QRect filenameOverlayRect(const QFontMetrics &metrics, const QRect &pane,
                                 const QString &text)
{
    if (text.isEmpty() || pane.width() < 24 || pane.height() < 16)
        return {};
    const int maxWidth = std::max(16, pane.width() - kFilenameOverlayPad * 2);
    const QRect bound =
        metrics.boundingRect(QRect(0, 0, maxWidth, kFilenameOverlayMaxHeight),
                             Qt::TextWrapAnywhere | Qt::AlignLeft | Qt::AlignTop, text);
    const int height =
        std::clamp(bound.height() + 6, metrics.height() + 6, kFilenameOverlayMaxHeight);
    return QRect(pane.left() + kFilenameOverlayPad, pane.top() + kFilenameOverlayPad, maxWidth,
                 height);
}

inline QRect drawFilenameOverlay(QPainter &p, const QRect &pane, const QString &text)
{
    if (text.isEmpty() || pane.width() < 24)
        return {};
    QFont font = p.font();
    font.setBold(true);
    font.setPointSize(9);
    p.setFont(font);
    const QRect box = filenameOverlayRect(QFontMetrics(font), pane, text);
    if (box.isEmpty())
        return {};
    p.fillRect(box, QColor(0, 0, 0, 110));
    p.setPen(Qt::white);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::TextWrapAnywhere | Qt::AlignLeft | Qt::AlignTop,
               text);
    return box;
}

inline QRect histogramOverlayRect(const QRect &pane, const QRect &filenameBox)
{
    if (pane.width() < 48 || pane.height() < 40)
        return {};
    const int width = std::min(160, std::max(72, pane.width() / 3));
    const int height = std::min(56, std::max(36, pane.height() / 5));
    const int top =
        filenameBox.isEmpty() ? pane.top() + 4 : filenameBox.bottom() + 4;
    if (top + height > pane.bottom() - 4)
        return {};
    return QRect(pane.left() + 4, top, width, height);
}

} // namespace mviewer::ui
