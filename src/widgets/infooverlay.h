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
    if (pane.width() < 80 || pane.height() < 80)
        return {};
    const int top = filenameBox.isEmpty() ? pane.top() + 6 : filenameBox.bottom() + 6;
    const int maxWidth = pane.width() - 12;
    const int maxHeight = pane.bottom() - 6 - top;
    if (maxWidth < 80 || maxHeight < 64)
        return {};
    const int width = std::min(maxWidth, std::max(220, pane.width() / 2));
    const int height = std::min(maxHeight, std::min(180, std::max(110, pane.height() / 3)));
    return QRect(pane.left() + 6, top, width, height);
}

} // namespace mviewer::ui
