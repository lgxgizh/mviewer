#pragma once

#include "domain/SelectionMapping.h"

#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QSize>

#include <algorithm>

namespace mviewer::ui
{

inline QRectF roiPresentationRect(const mviewer::domain::Selection &selection,
                                  const QSize &sourceSize, const QRectF &destination)
{
    const auto mapped = mviewer::domain::selectionToPresentation(
        selection, {destination.x(), destination.y(), destination.width(), destination.height()},
        sourceSize.width(), sourceSize.height());
    return QRectF(mapped.x, mapped.y, mapped.width, mapped.height);
}

inline void drawROIOverlay(QPainter &p, const mviewer::domain::Selection &selection,
                           const QSize &sourceSize, const QRectF &destination,
                           bool drawHandles = true)
{
    const QRectF box = roiPresentationRect(selection, sourceSize, destination);
    if (box.isEmpty())
        return;
    p.save();
    QPen shadow(QColor(0, 0, 0, 220), 3);
    shadow.setCosmetic(true);
    p.setPen(shadow);
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    QPen accent(QColor(0xff, 0xd2, 0x33), 1);
    accent.setCosmetic(true);
    p.setPen(accent);
    p.drawRect(box);

    if (drawHandles)
    {
        constexpr double handleSize = 6.0;
        const QPointF points[] = {box.topLeft(),
                                  QPointF(box.center().x(), box.top()),
                                  box.topRight(),
                                  QPointF(box.left(), box.center().y()),
                                  QPointF(box.right(), box.center().y()),
                                  box.bottomLeft(),
                                  QPointF(box.center().x(), box.bottom()),
                                  box.bottomRight()};
        p.setPen(QPen(QColor(0, 0, 0, 220), 1));
        p.setBrush(QColor(0xff, 0xd2, 0x33));
        for (const QPointF &point : points)
            p.drawRect(QRectF(point.x() - handleSize / 2.0, point.y() - handleSize / 2.0,
                              handleSize, handleSize));
    }

    QFont font = p.font();
    font.setPointSize(8);
    font.setBold(true);
    p.setFont(font);
    const QString label = QStringLiteral("ROI %1×%2").arg(selection.width).arg(selection.height);
    const int labelWidth = p.fontMetrics().horizontalAdvance(label) + 8;
    const QRectF labelRect(box.left(), std::max(0.0, box.top() - 18.0), labelWidth, 16.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(labelRect, 2, 2);
    p.setPen(Qt::white);
    p.drawText(labelRect, Qt::AlignCenter, label);
    p.restore();
}

} // namespace mviewer::ui
