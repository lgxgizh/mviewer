#pragma once

#include <QImage>
#include <QWidget>

// RawImageView holds a QImage and renders it scaled to fit the widget
// size.  Supports zoom/pan via QPainter transforms in paintEvent:
//   wheel  -> zoom around cursor
//   drag   -> pan
class RawImageView : public QWidget
{
    Q_OBJECT

public:
    explicit RawImageView(QWidget* parent = nullptr);

    const QImage& image() const { return m_image; }
    void setImage(const QImage& img);
    void clear();

    double scale() const { return m_scale; }
    const QPointF& offset() const { return m_offset; }

    // Apply to transform from image coords -> widget coords
    void setTransform(double scale, const QPointF& offset);
    // Constrain offset so image stays visible
    void clampOffset();

signals:
    void scaleChanged(double scale);

public slots:
    void zoom(double factor, const QPointF& anchor = {});
    void resetFit();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void computeFit();

    QImage m_image;
    double m_scale = 1.0;
    double m_fitScale = 1.0;
    QPointF m_offset;
    bool m_dragging = false;
    QPoint m_lastMouse;
};
