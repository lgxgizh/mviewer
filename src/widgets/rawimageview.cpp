#include "widgets/rawimageview.h"

#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

RawImageView::RawImageView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(64, 64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
}

void RawImageView::setImage(const QImage &img)
{
    m_image = img;
    m_sizeMismatch = false;
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

    // M16.1: synced crosshair at the shared image-space position (n/n compare).
    if (m_crosshairOn)
        drawCrosshair(p, cx, cy, dw, dh);

    // M16.1: focus-lock highlight — draw a thick accent border when this cell
    // is the locked reference.
    if (m_focused)
    {
        QPen pen(QColor(0xFF, 0xB0, 0x20), 3);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, width() - 3.0, height() - 3.0));
    }

    // A-4.3: Pixel Link markers — numbered dots at image-space points.
    if (!m_linkMarkers.isEmpty() && m_scale > 0.0)
    {
        const double imgLeft = cx - dw / 2.0;
        const double imgTop = cy - dh / 2.0;
        for (int i = 0; i < m_linkMarkers.size(); ++i)
        {
            const QPointF &pt = m_linkMarkers[i];
            const double wx = imgLeft + pt.x() * m_scale;
            const double wy = imgTop + pt.y() * m_scale;
            if (!std::isfinite(wx) || !std::isfinite(wy))
                continue;
            // Outer ring
            p.setPen(QPen(QColor(255, 255, 255), 2));
            p.setBrush(QColor(0xFF, 0x44, 0x44));
            p.drawEllipse(QPointF(wx, wy), 6, 6);
            // Index label
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setBold(true);
            f.setPointSize(8);
            p.setFont(f);
            p.drawText(QRectF(wx - 10, wy - 20, 20, 14), Qt::AlignCenter, QString::number(i + 1));
        }
    }

    // H1: visible corner badge when the diff for this cell was skipped because its
    // size does not match the base image. Without this the diff overlay silently
    // disappears and the user may misread it as "no difference".
    if (m_sizeMismatch)
    {
        p.save();
        QFont bf = p.font();
        bf.setBold(true);
        bf.setPointSize(9);
        p.setFont(bf);
        const QString txt = tr("尺寸不匹配");
        const int ts = p.fontMetrics().horizontalAdvance(txt);
        const int bw = ts + 12, bh = 18;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 40, 40, 235));
        p.drawRoundedRect(6, 6, bw, bh, 3, 3);
        p.setPen(Qt::white);
        p.drawText(QRect(6, 6, bw, bh), Qt::AlignCenter, txt);
        p.restore();
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
            const QPointF imagePos = widgetToImage(ev->pos());
            const int ix = qFloor(imagePos.x());
            const int iy = qFloor(imagePos.y());
            if (ix >= 0 && iy >= 0 && ix < m_image.width() && iy < m_image.height())
            {
                const QRgb c = m_image.pixel(ix, iy);
                emit pixelInfo(ix, iy, qRed(c), qGreen(c), qBlue(c), true);
                // M16.1 (n/n crosshair): mirror the cursor position to all cells.
                emit crosshairMoved(QPointF(ix, iy));
            }
            else
            {
                emit pixelInfo(-1, -1, 0, 0, 0, false);
                emit crosshairMoved(QPointF(-1, -1));
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

void RawImageView::mouseDoubleClickEvent(QMouseEvent *ev)
{
    // M16.1: toggle this cell as the locked reference (focus-lock, n/1).
    Q_UNUSED(ev);
    emit focusRequested(m_cellIndex);
}

void RawImageView::leaveEvent(QEvent *ev)
{
    QWidget::leaveEvent(ev);
    // Cursor left the cell: clear the synced crosshair everywhere.
    emit crosshairMoved(QPointF(-1, -1));
}

void RawImageView::drawCrosshair(QPainter &p, double cx, double cy, double dw, double dh) const
{
    if (m_image.isNull() || m_scale <= 0.0)
        return;
    // Map the image-space crosshair point to widget coords (same transform as
    // the rendered image, so it tracks zoom/pan).
    const double wx = cx - dw / 2.0 + m_crosshair.x() * m_scale;
    const double wy = cy - dh / 2.0 + m_crosshair.y() * m_scale;
    if (!std::isfinite(wx) || !std::isfinite(wy))
        return;
    QPen pen(QColor(0x33, 0xDD, 0xFF), 1, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.drawLine(QPointF(0, wy), QPointF(width(), wy));
    p.drawLine(QPointF(wx, 0), QPointF(wx, height()));
    // Center marker
    p.setBrush(QColor(0x33, 0xDD, 0xFF));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(wx, wy), 3, 3);
}

void RawImageView::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    computeFit();
    clampOffset();
}

QPointF RawImageView::widgetToImage(const QPoint &pos) const
{
    // Top-left origin image coords, matching paintEvent's draw rect:
    //   imgLeft = cx - dw/2, imgTop = cy - dh/2
    if (m_scale <= 0.0 || m_image.isNull())
        return {};
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const double dw = m_image.width() * m_scale;
    const double dh = m_image.height() * m_scale;
    const double imgLeft = cx - dw / 2.0;
    const double imgTop = cy - dh / 2.0;
    return QPointF((pos.x() - imgLeft) / m_scale, (pos.y() - imgTop) / m_scale);
}
