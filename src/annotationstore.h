#pragma once

#include <QColor>
#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mviewer::ui
{

// One reviewer markup primitive. Geometry is stored in IMAGE pixel coordinates
// (not screen coordinates) so an annotation survives zoom/pan and can be
// rendered onto the source-resolution raster at export time.
enum class AnnotationKind
{
    Rect,
    Arrow,
    Text
};

struct Annotation
{
    AnnotationKind kind = AnnotationKind::Rect;
    QRectF rect;  // Rect: the box; Text: the label anchor box
    QLineF line;  // Arrow: start -> end
    QString text; // Text: the label body
    QColor color = QColor(255, 72, 72);
};

// Per-image markup, persisted next to the source as
// "<stem>.mviewer-annotations.json" so a review can be shared without touching
// the original file.
class AnnotationStore
{
  public:
    explicit AnnotationStore(QString sourcePath = {});

    void setSourcePath(QString path);
    QString sourcePath() const
    {
        return m_sourcePath;
    }
    QString sidecarPath() const;

    QVector<Annotation> annotations() const
    {
        return m_annotations;
    }
    void setAnnotations(const QVector<Annotation> &annotations);
    void add(const Annotation &annotation);
    void removeAt(int index);
    void clear();

    /// Hit test in image coordinates; -1 when nothing is under the point.
    int indexAt(const QPointF &imagePoint) const;

    bool load();
    bool save() const;

  private:
    QString m_sourcePath;
    QVector<Annotation> m_annotations;
};

} // namespace mviewer::ui
