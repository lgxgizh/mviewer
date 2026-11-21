#include "cvoverlay.h"

#include "core/render/Viewport.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace mviewer::ui
{

bool CvOverlay::loadFromJsonFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonArray boxes = doc.object().value(QStringLiteral("boxes")).toArray();

    QVector<QRectF> parsed;
    QStringList labels;
    parsed.reserve(boxes.size());
    labels.reserve(boxes.size());
    for (const QJsonValue &value : boxes)
    {
        const QJsonArray item = value.toArray();
        if (item.size() < 4)
            continue;
        const QRectF rect(item.at(0).toDouble(), item.at(1).toDouble(), item.at(2).toDouble(),
                          item.at(3).toDouble());
        if (rect.width() <= 0.0 || rect.height() <= 0.0)
            continue;
        parsed.append(rect);
        labels.append(item.size() >= 5 ? item.at(4).toString() : QString());
    }
    m_boxes = std::move(parsed);
    m_labels = std::move(labels);
    return true;
}

void CvOverlay::clear()
{
    m_boxes.clear();
    m_labels.clear();
}

bool CvOverlay::isEmpty() const
{
    return m_boxes.isEmpty();
}

int CvOverlay::count() const
{
    return m_boxes.size();
}

void CvOverlay::draw(QPainter &painter, const Viewport &view) const
{
    if (m_boxes.isEmpty() || !(view.scale > 0.0))
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_boxes.size(); ++i)
    {
        const QRectF &box = m_boxes.at(i);
        const QPointF topLeft(box.x() * view.scale + view.offsetX,
                              box.y() * view.scale + view.offsetY);
        const QRectF screen(topLeft, QSizeF(box.width() * view.scale, box.height() * view.scale));
        painter.setPen(QPen(QColor(0, 220, 120), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(screen);
        const QString label = m_labels.value(i);
        if (!label.isEmpty())
            painter.drawText(screen.topLeft() + QPointF(4, -4), label);
    }
    painter.restore();
}

} // namespace mviewer::ui
