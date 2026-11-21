#pragma once

// CV detection overlay (report feature 10, extracted from ImageViewer).
//
// Owns the box list loaded from a JSON sidecar and the drawing of that list.
// Keeping it out of ImageViewer is the first step of the God Class split: the
// viewer keeps the pixels and the view transform, this class keeps one
// self-contained responsibility.

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;
struct Viewport;

namespace mviewer::ui
{

class CvOverlay
{
  public:
    /// Read `{"boxes": [[x, y, w, h, label], ...]}` in image coordinates.
    /// Returns false and fills `error` when the file cannot be read or parsed.
    bool loadFromJsonFile(const QString &path, QString *error);

    void clear();
    bool isEmpty() const;
    int count() const;

    /// Draw the boxes (and labels) through the given view transform.
    void draw(QPainter &painter, const Viewport &view) const;

  private:
    QVector<QRectF> m_boxes;
    QStringList m_labels;
};

} // namespace mviewer::ui
