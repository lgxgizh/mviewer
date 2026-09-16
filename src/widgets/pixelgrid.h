#pragma once

#include "core/analysis/PixelGrid.h"

#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QVarLengthArray>

namespace mviewer::ui
{

inline void drawPixelGrid(QPainter &p, const QRectF &imageDest, int srcX, int srcY, int srcW,
                          int srcH, const QRectF &visible)
{
    const auto lines = mviewer::enumeratePixelGrid(
        imageDest.x(), imageDest.y(), imageDest.width(), imageDest.height(), srcX, srcY, srcW, srcH,
        visible.x(), visible.y(), visible.width(), visible.height());
    if (lines.empty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(255, 255, 255, 72), 1);
    pen.setCosmetic(true);
    p.setPen(pen);
    QVarLengthArray<QLineF, 256> qlines;
    qlines.reserve(static_cast<qsizetype>(lines.size()));
    for (const auto &line : lines)
        qlines.append(QLineF(line.x1, line.y1, line.x2, line.y2));
    p.drawLines(qlines.constData(), static_cast<int>(qlines.size()));
    p.restore();
}

} // namespace mviewer::ui
