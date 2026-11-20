#include "annotationstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mviewer::ui
{

namespace
{
constexpr int kAnnotationFormatVersion = 1;

QString kindName(AnnotationKind kind)
{
    switch (kind)
    {
    case AnnotationKind::Rect:
        return QStringLiteral("rect");
    case AnnotationKind::Arrow:
        return QStringLiteral("arrow");
    case AnnotationKind::Text:
        return QStringLiteral("text");
    }
    return QStringLiteral("rect");
}

AnnotationKind kindFromName(const QString &name)
{
    if (name == QLatin1String("arrow"))
        return AnnotationKind::Arrow;
    if (name == QLatin1String("text"))
        return AnnotationKind::Text;
    return AnnotationKind::Rect;
}

QJsonObject toJson(const Annotation &a)
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), kindName(a.kind));
    o.insert(QStringLiteral("color"), a.color.name(QColor::HexRgb));
    o.insert(QStringLiteral("text"), a.text);
    if (a.kind == AnnotationKind::Arrow)
    {
        QJsonArray line{a.line.x1(), a.line.y1(), a.line.x2(), a.line.y2()};
        o.insert(QStringLiteral("line"), line);
    }
    else
    {
        QJsonArray rect{a.rect.x(), a.rect.y(), a.rect.width(), a.rect.height()};
        o.insert(QStringLiteral("rect"), rect);
    }
    return o;
}

Annotation fromJson(const QJsonObject &o)
{
    Annotation a;
    a.kind = kindFromName(o.value(QStringLiteral("kind")).toString());
    a.text = o.value(QStringLiteral("text")).toString();
    a.color = QColor(o.value(QStringLiteral("color")).toString());
    if (!a.color.isValid())
        a.color = QColor(255, 72, 72);
    if (a.kind == AnnotationKind::Arrow)
    {
        const QJsonArray line = o.value(QStringLiteral("line")).toArray();
        if (line.size() == 4)
            a.line = QLineF(line.at(0).toDouble(), line.at(1).toDouble(), line.at(2).toDouble(),
                            line.at(3).toDouble());
    }
    else
    {
        const QJsonArray rect = o.value(QStringLiteral("rect")).toArray();
        if (rect.size() == 4)
            a.rect = QRectF(rect.at(0).toDouble(), rect.at(1).toDouble(), rect.at(2).toDouble(),
                            rect.at(3).toDouble());
    }
    return a;
}
} // namespace

AnnotationStore::AnnotationStore(QString sourcePath) : m_sourcePath(std::move(sourcePath))
{
}

void AnnotationStore::setSourcePath(QString path)
{
    if (m_sourcePath == path)
        return;
    m_sourcePath = std::move(path);
    m_annotations.clear();
}

QString AnnotationStore::sidecarPath() const
{
    if (m_sourcePath.isEmpty())
        return {};
    const QFileInfo info(m_sourcePath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
           QStringLiteral(".mviewer-annotations.json");
}

void AnnotationStore::setAnnotations(const QVector<Annotation> &annotations)
{
    m_annotations = annotations;
}

void AnnotationStore::add(const Annotation &annotation)
{
    m_annotations.append(annotation);
}

void AnnotationStore::removeAt(int index)
{
    if (index >= 0 && index < m_annotations.size())
        m_annotations.removeAt(index);
}

void AnnotationStore::clear()
{
    m_annotations.clear();
}

int AnnotationStore::indexAt(const QPointF &imagePoint) const
{
    // Topmost first: the last drawn annotation wins, like a layer stack.
    for (int i = m_annotations.size() - 1; i >= 0; --i)
    {
        const Annotation &a = m_annotations.at(i);
        if (a.kind == AnnotationKind::Arrow)
        {
            const QRectF box = QRectF(a.line.p1(), a.line.p2()).normalized();
            if (box.adjusted(-6, -6, 6, 6).contains(imagePoint))
                return i;
        }
        else if (a.rect.adjusted(-4, -4, 4, 4).contains(imagePoint))
        {
            return i;
        }
    }
    return -1;
}

bool AnnotationStore::load()
{
    m_annotations.clear();
    const QString path = sidecarPath();
    if (path.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return false;
    const QJsonArray items = doc.object().value(QStringLiteral("annotations")).toArray();
    m_annotations.reserve(items.size());
    for (const QJsonValue &value : items)
    {
        if (value.isObject())
            m_annotations.append(fromJson(value.toObject()));
    }
    return true;
}

bool AnnotationStore::save() const
{
    const QString path = sidecarPath();
    if (path.isEmpty())
        return false;
    QJsonArray items;
    for (const Annotation &a : m_annotations)
        items.append(toJson(a));
    QJsonObject root;
    root.insert(QStringLiteral("version"), kAnnotationFormatVersion);
    root.insert(QStringLiteral("source"), QFileInfo(m_sourcePath).fileName());
    root.insert(QStringLiteral("annotations"), items);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool ok = file.write(payload) == payload.size();
    file.close();
    return ok;
}

} // namespace mviewer::ui
