#include "widgets/rawimageview.h"
#include "widgets/infooverlay.h"
#include "widgets/pixelgrid.h"
#include "widgets/roioverlay.h"

#include "core/analysis/ImageOverlay.h"
#include "core/image/QtConvert.h"
#include "core/render/ZoomPercent.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
// Defensive bounds for the viewport-bounded base surface. A pane larger than
// this is beyond any real display (16384 px per side, 64M device pixels ≈
// 256 MiB of ARGB); the cache simply falls back to direct paint.
Qt::CursorShape cursorForSelectionHandle(mviewer::domain::SelectionHandle handle)
{
    switch (handle)
    {
    case mviewer::domain::SelectionHandle::Move:
        return Qt::SizeAllCursor;
    case mviewer::domain::SelectionHandle::Left:
    case mviewer::domain::SelectionHandle::Right:
        return Qt::SizeHorCursor;
    case mviewer::domain::SelectionHandle::Top:
    case mviewer::domain::SelectionHandle::Bottom:
        return Qt::SizeVerCursor;
    case mviewer::domain::SelectionHandle::TopLeft:
    case mviewer::domain::SelectionHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case mviewer::domain::SelectionHandle::TopRight:
    case mviewer::domain::SelectionHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    default:
        return Qt::OpenHandCursor;
    }
}
} // namespace

RawImageView::RawImageView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(64, 64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
}

void RawImageView::setImage(const QImage &img) { setImage(img, {}); }

void RawImageView::setImage(const QImage &img, const QSize &sourceSize)
{
    const QSize fullSize = sourceSize.isValid() ? sourceSize : img.size();
    setImage(img, fullSize, QRect(QPoint(0, 0), fullSize));
}

void RawImageView::setImage(const QImage &img, const QSize &sourceSize, const QRect &sourceRect)
{
    clearTransientDisplay();
    m_image = img;
    m_sourceSize = sourceSize.isValid() ? sourceSize : img.size();
    const QRect fullRect(QPoint(0, 0), m_sourceSize);
    m_sourceRect = sourceRect.isValid() ? sourceRect.normalized().intersected(fullRect) : fullRect;
    if (m_sourceRect.isEmpty())
        m_sourceRect = fullRect;
    m_sizeMismatch = false;
    rebuildFilteredDisplay();
    releaseBaseSurface();
    resetFit();
    update();
}

void RawImageView::clear()
{
    clearTransientDisplay();
    m_image = QImage();
    m_filteredDisplay = QImage();
    m_metricBadge.clear();
    m_sourceSize = {};
    m_sourceRect = {};
    m_scale = m_fitScale = 1.0;
    m_offset = {};
    releaseBaseSurface();
    update();
}

void RawImageView::setMetricBadge(const QString &text)
{
    if (m_metricBadge == text)
        return;
    m_metricBadge = text;
    update();
}

void RawImageView::setTransientDisplay(const QImage &img, const QSize &sourceSize,
                                       const QRect &sourceRect)
{
    if (img.isNull())
    {
        clearTransientDisplay();
        return;
    }
    const QSize fullSize = sourceSize.isValid() ? sourceSize : img.size();
    const QRect fullRect(QPoint(0, 0), fullSize);
    QRect covered = sourceRect.isValid() ? sourceRect.normalized().intersected(fullRect) : fullRect;
    if (covered.isEmpty())
        covered = fullRect;
    m_transientImage = img;
    m_transientSourceSize = fullSize;
    m_transientSourceRect = covered;
    m_transientRenderScale = 0.0;
    rebuildFilteredDisplay();
    releaseBaseSurface();
    update();
}

void RawImageView::setTransientRenderScale(double scale)
{
    if (!std::isfinite(scale) || !(scale > 0.0))
        scale = 0.0;
    if (m_transientRenderScale == scale)
        return;
    m_transientRenderScale = scale;
    releaseBaseSurface();
    update();
}

void RawImageView::clearTransientDisplay()
{
    if (m_transientImage.isNull() && !(m_transientRenderScale > 0.0))
        return;
    m_transientImage = QImage();
    m_transientSourceSize = {};
    m_transientSourceRect = {};
    m_transientRenderScale = 0.0;
    rebuildFilteredDisplay();
    releaseBaseSurface();
    update();
}

void RawImageView::setDisplayOverlay(mviewer::OverlayMode mode)
{
    if (m_displayOverlay == mode)
        return;
    m_displayOverlay = mode;
    rebuildFilteredDisplay();
    releaseBaseSurface();
    update();
}

void RawImageView::setFilenameOverlay(const QString &text, bool visible)
{
    if (m_filenameOverlayText == text && m_filenameOverlayVisible == visible)
        return;
    m_filenameOverlayText = text;
    m_filenameOverlayVisible = visible;
    update();
}

void RawImageView::rebuildFilteredDisplay()
{
    m_filteredDisplay = QImage();
    if (m_displayOverlay == mviewer::OverlayMode::None)
        return;
    const QImage &src = displayImage();
    if (src.isNull())
        return;
    ImageData data = mvcore::fromQImage(src);
    if (data.isNull())
        return;
    mviewer::applyOverlay(data, m_displayOverlay, 2);
    m_filteredDisplay = mvcore::toQImage(data);
}

QSize RawImageView::renderSourceSize() const
{
    return m_transientImage.isNull() ? m_sourceSize : m_transientSourceSize;
}

QRect RawImageView::renderSourceRect() const
{
    return m_transientImage.isNull() ? m_sourceRect : m_transientSourceRect;
}

void RawImageView::setOverlay(const QImage &overlay, double alpha)
{
    m_overlay = overlay;
    m_overlayAlpha = alpha;
    update();
}

void RawImageView::setTransform(double scale, const QPointF &offset)
{
    // CompareWorkspace pushes the same transform to every pane on each of its
    // own paints. Decide the no-op on the *effective* (clamped) transform: an
    // identical push — or an out-of-range input that clamps back to the current
    // offset — must neither invalidate the cached surface nor enqueue another
    // child repaint. Exact doubles are safe here: a real change always produces
    // a different value, so equality cannot mask a genuine scale/offset update.
    const double prevScale = m_scale;
    const QPointF prevOffset = m_offset;
    m_scale = scale;
    m_offset = offset;
    clampOffset();
    if (m_scale == prevScale && m_offset == prevOffset)
        return;
    update();
    emit transformChanged();
}

void RawImageView::clampOffset()
{
    if (m_image.isNull() || !m_sourceSize.isValid())
    {
        m_offset = {};
        return;
    }
    // Allow panning within a reasonable range
    const double maxOffX = qMax(0.0, m_sourceSize.width() * m_scale) / 2.0 + width();
    const double maxOffY = qMax(0.0, m_sourceSize.height() * m_scale) / 2.0 + height();
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
    m_offset = anchorPt - QPointF(width() / 2.0, height() / 2.0) - imgPt * newScale;
    m_scale = newScale;
    clampOffset();
    emit scaleChanged(m_scale);
    emit transformChanged();
    update();
}

void RawImageView::resetFit()
{
    m_scale = m_fitScale = 1.0;
    m_offset = {};
    if (!m_image.isNull())
        computeFit();
    update();
    emit transformChanged();
}

void RawImageView::computeFit()
{
    if (m_image.isNull() || width() <= 0 || height() <= 0)
    {
        m_fitScale = 1.0;
        return;
    }
    if (!m_sourceSize.isValid())
    {
        m_fitScale = 1.0;
        return;
    }
    m_fitScale = std::min(static_cast<double>(width()) / m_sourceSize.width(),
                          static_cast<double>(height()) / m_sourceSize.height());
    m_scale = m_fitScale;
    m_offset = {};
}

void RawImageView::drawCornerBadge(QPainter &p, const QString &txt, const QColor &bg, bool right)
{
    QFont bf = p.font();
    bf.setBold(true);
    bf.setPointSize(9);
    p.setFont(bf);
    const int ts = p.fontMetrics().horizontalAdvance(txt);
    const int bw = ts + 12, bh = 18;
    const int x = right ? width() - bw - 6 : 6;
    const int y = 6;
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(x, y, bw, bh, 3, 3);
    p.setPen(Qt::white);
    p.drawText(QRect(x, y, bw, bh), Qt::AlignCenter, txt);
}

void RawImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Dark));

    if (displayImage().isNull())
        return;

    // During active dragging/panning, bypass the offscreen baseSurface buffer
    // (allocating/clearing a 4K surface on every mouse move frame wastes tens of MB of memory memset).
    // Direct drawing to the viewport is faster and smoother.
    if (m_dragging)
    {
        m_baseSurfaceValid = false;
        drawBaseLayer(p);
    }
    else
    {
        // Rasterize the static base once per input change. A wheel burst reuses
        // that surface (scaled) instead of rebuilding it on every notch.
        if (!paintBaseSurface(p))
            drawBaseLayer(p);
    }

    // Geometry for the live annotation layer below (same transform as the image).
    const QSize sourceSize = renderSourceSize();
    const double presented = presentedScale();
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const int dw = qRound(sourceSize.width() * presented);
    const int dh = qRound(sourceSize.height() * presented);
    drawLiveOverlays(p, cx, cy, dw, dh, sourceSize, presented);
}

void RawImageView::drawLiveOverlays(QPainter &p, double cx, double cy, int dw, int dh,
                                   const QSize &sourceSize, double presented)
{
    if (mviewer::pixelGridVisible(presented) && sourceSize.width() > 0 && sourceSize.height() > 0)
    {
        mviewer::ui::drawPixelGrid(
            p,
            QRectF(cx - dw / 2.0, cy - dh / 2.0, static_cast<double>(dw), static_cast<double>(dh)),
            0, 0, sourceSize.width(), sourceSize.height(), QRectF(rect()));
    }

    if (m_transientImage.isNull())
    {
        mviewer::ui::drawROIOverlay(
            p, m_selection, m_sourceSize,
            QRectF(cx - dw / 2.0, cy - dh / 2.0, static_cast<double>(dw), static_cast<double>(dh)),
            true, m_paneTag);

        if (m_crosshairOn)
            drawCrosshair(p, cx, cy, dw, dh);

        if (m_focused)
        {
            QPen pen(QColor(0xFF, 0xB0, 0x20), 3);
            pen.setCosmetic(true);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(1.5, 1.5, width() - 3.0, height() - 3.0));
        }

        if (!m_linkMarkers.isEmpty() && m_scale > 0.0)
        {
            for (int i = 0; i < m_linkMarkers.size(); ++i)
            {
                const QPointF &pt = m_linkMarkers[i];
                const QPointF widgetPoint = sourcePointToWidget(pt);
                if (!std::isfinite(widgetPoint.x()) || !std::isfinite(widgetPoint.y()))
                    continue;
                const double wx = widgetPoint.x();
                const double wy = widgetPoint.y();
                p.setPen(QPen(QColor(255, 255, 255), 2));
                p.setBrush(QColor(0xFF, 0x44, 0x44));
                p.drawEllipse(QPointF(wx, wy), 6, 6);
                p.setPen(Qt::white);
                QFont f = p.font();
                f.setBold(true);
                f.setPointSize(8);
                p.setFont(f);
                p.drawText(QRectF(wx - 10, wy - 20, 20, 14), Qt::AlignCenter, QString::number(i + 1));
            }
        }
    }

    if (m_sizeMismatch)
    {
        p.save();
        drawCornerBadge(p, tr("尺寸不匹配"), QColor(200, 40, 40, 235), false);
        p.restore();
    }

    if (m_softLoading)
    {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 28));
        p.drawRect(rect());
        drawCornerBadge(p, tr("加载中"), QColor(30, 90, 180, 210), true);
        p.restore();
    }
    else if (!m_metricBadge.isEmpty())
    {
        p.save();
        drawCornerBadge(p, m_metricBadge, QColor(20, 40, 60, 215), true);
        p.restore();
    }

    if (m_filenameOverlayVisible && !m_filenameOverlayText.isEmpty())
        mviewer::ui::drawFilenameOverlay(p, rect(), m_filenameOverlayText);
    const std::string zoomText = mviewer::core::formatZoomPercent(presented);
    if (!zoomText.empty())
        mviewer::ui::drawZoomPercentBadge(p, rect(), QString::fromLatin1(zoomText.c_str()), false);
}

void RawImageView::wheelEvent(QWheelEvent *ev)
{
    // Compare mode usually consumes the wheel in CompareWorkspace and reaches
    // the same coalesce via setTransform. Direct wheels share ensureBaseSurface.
    // Prefer angleDelta, then pixelDelta, so a zero angle does not zoom out.
    const int delta = ev->angleDelta().y() != 0 ? ev->angleDelta().y() : ev->pixelDelta().y();
    if (delta == 0)
    {
        ev->accept();
        return;
    }
    const double factor = delta > 0 ? 1.25 : 1.0 / 1.25;
    zoom(factor, ev->position());
}

void RawImageView::drawBaseLayer(QPainter &p)
{
    // Single source of truth for the base image + diff overlay geometry shared
    // by the cached viewport surface and the direct-draw fallback. Matches the
    // pre-cache paintEvent rendering exactly.
    const QImage &image = presentationImage();
    const QSize sourceSize = renderSourceSize();
    const QRect sourceRect = renderSourceRect();
    const double scale = presentedScale();
    // Consistent interpolation: when magnified (scale >= 1.0), use nearest-neighbor
    // so engineers can inspect exact pixels; when downscaled (scale < 1.0),
    // use smooth bilinear to prevent downsample aliasing.
    // Crucially, interpolation must never depend on dragging/clicking state.
    const double effectiveScale =
        (image.width() > 0 && sourceRect.width() > 0)
            ? (static_cast<double>(sourceRect.width()) * scale / image.width())
            : scale;
    p.setRenderHint(QPainter::SmoothPixmapTransform, effectiveScale < 0.999);

    // Center in widget, then apply pan offset, then scale.
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const int dw = qRound(sourceSize.width() * scale);
    const int dh = qRound(sourceSize.height() * scale);
    const double sourceLeft = cx - dw / 2.0;
    const double sourceTop = cy - dh / 2.0;
    const QRectF coveredDest(sourceLeft + sourceRect.x() * scale,
                             sourceTop + sourceRect.y() * scale, sourceRect.width() * scale,
                             sourceRect.height() * scale);
    p.drawImage(coveredDest, image);

    // Difference/heatmap overlay (compare mode): same transform as the base image
    // so it tracks zoom/pan. The QImage is produced by the workspace from core-layer
    // data (DifferenceEngine::heatMap) — RawImageView performs no decoding here.
    if (!m_overlay.isNull())
    {
        p.save();
        p.setOpacity(m_overlayAlpha);
        // The overlay is a display LOD of the same source geometry. Draw it
        // into the base destination so a smaller materialization remains
        // registered instead of being centered as a smaller image.
        p.drawImage(QRectF(cx - dw / 2.0, cy - dh / 2.0, dw, dh), m_overlay);
        p.restore();
    }
}

void RawImageView::mousePressEvent(QMouseEvent *ev)
{
    if (!m_transientImage.isNull())
    {
        ev->accept();
        return;
    }
    if (ev->button() == Qt::RightButton)
    {
        // Begin box selection (image coords) instead of panning. Keep the old
        // selection visible until the drag crosses the platform threshold so
        // an ordinary right click cannot accidentally clear a measurement.
        m_selecting = true;
        m_selectionMoved = false;
        m_selectPressPos = ev->pos();
        m_selectStart = widgetToImage(ev->pos());
        m_selectOrigin = m_selection;
        const double tolerance = m_scale > 0.0 ? 8.0 / m_scale : 0.0;
        m_selectHandle = mviewer::domain::hitTestSelection(m_selection, m_selectStart.x(),
                                                           m_selectStart.y(), tolerance, tolerance);
        if (m_selectHandle == mviewer::domain::SelectionHandle::None)
            m_selectHandle = mviewer::domain::SelectionHandle::Create;
        ev->accept();
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
    if (!m_transientImage.isNull())
    {
        ev->accept();
        return;
    }
    if (m_selecting)
    {
        if (!m_selectionMoved && (ev->pos() - m_selectPressPos).manhattanLength() < 4)
        {
            ev->accept();
            return;
        }
        m_selectionMoved = true;
        const QPointF cur = widgetToImage(ev->pos());
        m_selection = mviewer::domain::updateSelectionInteraction(
            m_selectOrigin, m_selectHandle, m_selectStart.x(), m_selectStart.y(), cur.x(), cur.y(),
            m_sourceSize.width(), m_sourceSize.height());
        emit selectionPreviewChanged(m_selection);
        update();
        ev->accept();
        return;
    }
    if (!m_dragging)
    {
        const double tol = m_scale > 0.0 ? 8.0 / m_scale : 0.0;
        const auto handle =
            (!m_selection.isEmpty() && tol > 0.0)
                ? mviewer::domain::hitTestSelection(m_selection, widgetToImage(ev->pos()).x(),
                                                    widgetToImage(ev->pos()).y(), tol, tol)
                : mviewer::domain::SelectionHandle::None;
        const Qt::CursorShape shape = cursorForSelectionHandle(handle);
        if (cursor().shape() != shape)
            setCursor(shape);

        // Hover: report the image-space pixel under the cursor for the inspector.
        if (!m_image.isNull() && m_scale > 0.0)
        {
            const QPointF imagePos = widgetToImage(ev->pos());
            const int ix = qFloor(imagePos.x());
            const int iy = qFloor(imagePos.y());
            if (ix >= 0 && iy >= 0 && ix < m_sourceSize.width() && iy < m_sourceSize.height())
            {
                if (ix != m_lastHoverPixelX || iy != m_lastHoverPixelY)
                {
                    m_lastHoverPixelX = ix;
                    m_lastHoverPixelY = iy;
                    // The pane image is a bounded display LOD. It is intentionally
                    // not an analysis source, so the legacy RGB payload is left
                    // empty; CompareWorkspace re-samples the ImageFrame by (ix,iy).
                    emit pixelInfo(ix, iy, 0, 0, 0, true);
                    // M16.1 (n/n crosshair): mirror the cursor position to all cells.
                    emit crosshairMoved(QPointF(ix, iy));
                }
            }
            else
            {
                if (m_lastHoverPixelX != -1 || m_lastHoverPixelY != -1)
                {
                    m_lastHoverPixelX = -1;
                    m_lastHoverPixelY = -1;
                    emit pixelInfo(-1, -1, 0, 0, 0, false);
                    emit crosshairMoved(QPointF(-1, -1));
                }
            }
        }
        return;
    }
    const QPoint delta = ev->pos() - m_lastMouse;
    m_lastMouse = ev->pos();
    m_offset += QPointF(delta);
    clampOffset();
    update();
    emit transformChanged();
}

void RawImageView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!m_transientImage.isNull())
    {
        m_dragging = false;
        m_selecting = false;
        setCursor(Qt::OpenHandCursor);
        ev->accept();
        return;
    }
    if (ev->button() == Qt::RightButton && m_selecting)
    {
        m_selecting = false;
        setCursor(Qt::OpenHandCursor);
        if (m_selectionMoved)
            emit selectionChanged(m_selection);
        m_selectHandle = mviewer::domain::SelectionHandle::None;
        ev->accept();
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        update();
    }
}

void RawImageView::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!m_transientImage.isNull())
    {
        ev->accept();
        return;
    }
    // M16.1: toggle this cell as the locked reference (focus-lock, n/1).
    Q_UNUSED(ev);
    emit focusRequested(m_cellIndex);
}

void RawImageView::leaveEvent(QEvent *ev)
{
    QWidget::leaveEvent(ev);
    setCursor(Qt::OpenHandCursor);
    m_lastHoverPixelX = -1;
    m_lastHoverPixelY = -1;
    // Cursor left the cell: clear the synced crosshair everywhere.
    emit crosshairMoved(QPointF(-1, -1));
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
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
// NOLINTEND(bugprone-easily-swappable-parameters)

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
    const double dw = m_sourceSize.width() * m_scale;
    const double dh = m_sourceSize.height() * m_scale;
    const double imgLeft = cx - dw / 2.0;
    const double imgTop = cy - dh / 2.0;
    return QPointF((pos.x() - imgLeft) / m_scale, (pos.y() - imgTop) / m_scale);
}

QPoint RawImageView::displayPointForSource(int x, int y) const
{
    if (m_image.isNull() || !m_sourceSize.isValid() || !m_sourceRect.isValid() ||
        x < m_sourceRect.x() || y < m_sourceRect.y() ||
        x >= m_sourceRect.x() + m_sourceRect.width() ||
        y >= m_sourceRect.y() + m_sourceRect.height())
        return {};
    const double sx = static_cast<double>(m_image.width()) / m_sourceRect.width();
    const double sy = static_cast<double>(m_image.height()) / m_sourceRect.height();
    return QPoint(qBound(0, qFloor((x - m_sourceRect.x()) * sx), m_image.width() - 1),
                  qBound(0, qFloor((y - m_sourceRect.y()) * sy), m_image.height() - 1));
}

QPointF RawImageView::sourcePointToWidget(const QPointF &sourcePoint) const
{
    if (m_image.isNull() || !m_sourceSize.isValid() || !(m_scale > 0.0) ||
        !std::isfinite(m_scale) || !std::isfinite(sourcePoint.x()) ||
        !std::isfinite(sourcePoint.y()))
    {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        return QPointF(invalid, invalid);
    }

    // The transform is center-relative and always describes the complete
    // source geometry. m_sourceRect only says which source region the bounded
    // raster covers; it must never change the source-to-widget mapping.
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const double sourceWidth = m_sourceSize.width() * m_scale;
    const double sourceHeight = m_sourceSize.height() * m_scale;
    return QPointF(cx - sourceWidth / 2.0 + sourcePoint.x() * m_scale,
                   cy - sourceHeight / 2.0 + sourcePoint.y() * m_scale);
}
