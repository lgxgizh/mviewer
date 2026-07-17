#include "widgets/rawimageview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

RawImageView::RawImageView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(64, 64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
}

void RawImageView::setImage(const QImage &img)
{
    m_image = img;
    resetFit();
    update();
}

void RawImageView::clear()
{
    m_image = QImage();
    m_scale = m_fitScale = 1.0;
    m_offset = {};
    update();
}

void RawImageView::setOverlay(const QImage &overlay, double alpha)
{
    m_overlay = overlay;
    m_overlayAlpha = alpha;
    update();
}

void RawImageView::setTransform(double scale, const QPointF &offset)
{
    m_scale = scale;
    m_offset = offset;
    clampOffset();
    update();
}

void RawImageView::clampOffset()
{
    if (m_image.isNull())
    {
        m_offset = {};
        return;
    }
    // Allow panning within a reasonable range
    const double maxOffX = qMax(0.0, m_image.width() * m_scale) / 2.0 + width();
    const double maxOffY = qMax(0.0, m_image.height() * m_scale) / 2.0 + height();
    m_offset.setX(qBound(-maxOffX, m_offset.x(), maxOffX));
    m_offset.setY(qBound(-maxOffY, m_offset.y(), maxOffY));
}

void RawImageView::zoom(double factor, const QPointF &anchor)
{
    const double newScale = qBound(m_fitScale * 0.05, m_scale * factor, m_fitScale * 50.0);
    if (newScale == m_scale)
        return;

    // Zoom around anchor point (widget coords)
    QPointF anchorPt = anchor;
    if (anchorPt.isNull())
        anchorPt = QPointF(width() / 2.0, height() / 2.0);

    // Keep the image point under anchor fixed
    const QPointF imgPt = (anchorPt - QPointF(width() / 2.0, height() / 2.0) - m_offset) / m_scale;
    const double ratio = newScale / m_scale;
    m_offset = anchorPt - QPointF(width() / 2.0, height() / 2.0) - imgPt * newScale;
    m_scale = newScale;
    clampOffset();
    emit scaleChanged(m_scale);
    update();
}

void RawImageView::resetFit()
{
    m_scale = m_fitScale = 1.0;
    m_offset = {};
    if (!m_image.isNull())
        computeFit();
    update();
}

void RawImageView::computeFit()
{
    if (m_image.isNull() || width() <= 0 || height() <= 0)
    {
        m_fitScale = 1.0;
        return;
    }
    m_fitScale = std::min(static_cast<double>(width()) / m_image.width(),
                          static_cast<double>(height()) / m_image.height());
    m_scale = m_fitScale;
    m_offset = {};
}

void RawImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Dark));

    if (m_image.isNull())
        return;

    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 4.0);

    // Center in widget, then apply pan offset, then scale
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const int dw = qRound(m_image.width() * m_scale);
    const int dh = qRound(m_image.height() * m_scale);

    p.drawImage(QRectF(cx - dw / 2.0, cy - dh / 2.0, dw, dh), m_image);

    // Difference/heatmap overlay (compare mode): same transform as the base image
    // so it tracks zoom/pan. The QImage is produced by the workspace from core-layer
    // data (DifferenceEngine::heatMap) — RawImageView performs no decoding here.
    if (!m_overlay.isNull())
    {
        const int ow = qRound(m_overlay.width() * m_scale);
        const int oh = qRound(m_overlay.height() * m_scale);
        p.save();
        p.setOpacity(m_overlayAlpha);
        p.drawImage(QRect(cx - dw / 2 + static_cast<int>((dw - ow) / 2.0),
                          cy - dh / 2 + static_cast<int>((dh - oh) / 2.0), ow, oh),
                    m_overlay);
        p.restore();
    }

    // ROI selection box (image coords -> widget coords, same transform as the image)
    if (!m_selection.isEmpty())
    {
        const double sx = static_cast<double>(dw) / m_image.width();
        const double sy = static_cast<double>(dh) / m_image.height();
        const QRect box(qRound(cx - dw / 2.0 + m_selection.x * sx),
                        qRound(cy - dh / 2.0 + m_selection.y * sy), qRound(m_selection.width * sx),
                        qRound(m_selection.height * sy));
        p.setPen(QPen(QColor(0xFF, 0x33, 0x33), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);
    }
}

void RawImageView::wheelEvent(QWheelEvent *ev)
{
    const double factor = ev->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
    zoom(factor, ev->position());
}

void RawImageView::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::RightButton)
    {
        // Begin box selection (image coords) instead of panning.
        m_selecting = true;
        m_selectStart = widgetToImage(ev->pos());
        m_selection = mviewer::domain::Selection{};
        update();
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_lastMouse = ev->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void RawImageView::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_selecting)
    {
        const QPointF cur = widgetToImage(ev->pos());
        const int x = qMin(m_selectStart.x(), cur.x());
        const int y = qMin(m_selectStart.y(), cur.y());
        const int w = qAbs(cur.x() - m_selectStart.x());
        const int h = qAbs(cur.y() - m_selectStart.y());
        m_selection = mviewer::domain::Selection{x, y, w, h};
        update();
        return;
    }
    if (!m_dragging)
    {
        // Hover: report the image-space pixel under the cursor for the inspector.
        if (!m_image.isNull() && m_scale > 0.0)
        {
            const double cx = width() / 2.0 + m_offset.x();
            const double cy = height() / 2.0 + m_offset.y();
            const int ix = qRound((ev->pos().x() - cx) / m_scale);
            const int iy = qRound((ev->pos().y() - cy) / m_scale);
            if (ix >= 0 && iy >= 0 && ix < m_image.width() && iy < m_image.height())
            {
                const QRgb c = m_image.pixel(ix, iy);
                emit pixelInfo(ix, iy, qRed(c), qGreen(c), qBlue(c), true);
            }
            else
            {
                emit pixelInfo(-1, -1, 0, 0, 0, false);
            }
        }
        return;
    }
    const QPoint delta = ev->pos() - m_lastMouse;
    m_lastMouse = ev->pos();
    m_offset += QPointF(delta);
    clampOffset();
    update();
}

void RawImageView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::RightButton && m_selecting)
    {
        m_selecting = false;
        emit selectionChanged(m_selection);
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

QPointF RawImageView::widgetToImage(const QPoint &pos) const
{
    if (m_scale <= 0.0 || m_image.isNull())
        return {};
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    return QPointF((pos.x() - cx) / m_scale, (pos.y() - cy) / m_scale);
}

void RawImageView::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    computeFit();
    clampOffset();
}
