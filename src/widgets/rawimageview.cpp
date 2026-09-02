#include "widgets/rawimageview.h"

#include <QEvent>
#include <QFont>
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
constexpr qint64 kMaxCacheDim = 16384;
constexpr qint64 kMaxSurfacePixels = qint64(64) * 1024 * 1024; // 64M px

void drawSelectionOverlay(QPainter &p, const mviewer::domain::Selection &selection,
                          const QSize &sourceSize, double cx, double cy, int dw, int dh)
{
    if (selection.isEmpty() || sourceSize.width() <= 0 || sourceSize.height() <= 0)
        return;
    const double sx = static_cast<double>(dw) / sourceSize.width();
    const double sy = static_cast<double>(dh) / sourceSize.height();
    const QRect box(qRound(cx - dw / 2.0 + selection.x * sx),
                    qRound(cy - dh / 2.0 + selection.y * sy), qRound(selection.width * sx),
                    qRound(selection.height * sy));
    // Two cosmetic outlines keep the ROI legible on both saturated dark and
    // bright imagery without obscuring the selected pixels.
    QPen shadow(QColor(0, 0, 0, 220), 3);
    shadow.setCosmetic(true);
    p.setPen(shadow);
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    QPen accent(QColor(0xFF, 0xD2, 0x33), 1);
    accent.setCosmetic(true);
    p.setPen(accent);
    p.drawRect(box);
    QFont labelFont = p.font();
    labelFont.setPointSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);
    const QString label = QStringLiteral("ROI %1×%2")
                              .arg(selection.width)
                              .arg(selection.height);
    const QRect labelRect(box.left(), std::max(0, box.top() - 18),
                          p.fontMetrics().horizontalAdvance(label) + 8, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(labelRect, 2, 2);
    p.setPen(Qt::white);
    p.drawText(labelRect, Qt::AlignCenter, label);
}
} // namespace

RawImageView::RawImageView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(64, 64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
}

void RawImageView::setImage(const QImage &img)
{
    setImage(img, {});
}

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
    releaseBaseSurface();
    resetFit();
    update();
}

void RawImageView::clear()
{
    clearTransientDisplay();
    m_image = QImage();
    m_sourceSize = {};
    m_sourceRect = {};
    m_scale = m_fitScale = 1.0;
    m_offset = {};
    releaseBaseSurface();
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
    releaseBaseSurface();
    update();
}

void RawImageView::clearTransientDisplay()
{
    if (m_transientImage.isNull())
        return;
    m_transientImage = QImage();
    m_transientSourceSize = {};
    m_transientSourceRect = {};
    releaseBaseSurface();
    update();
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

void RawImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Dark));

    if (displayImage().isNull())
        return;

    // Rasterize the static base image + diff overlay once per input change into
    // a viewport-bounded surface, then blit it; live annotations draw on top.
    ensureBaseSurface();
    if (m_baseSurfaceValid)
        p.drawImage(rect(), m_baseSurface);
    else
        drawBaseLayer(p);

    // Geometry for the live annotation layer below (same transform as the image).
    const QSize sourceSize = renderSourceSize();
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const int dw = qRound(sourceSize.width() * m_scale);
    const int dh = qRound(sourceSize.height() * m_scale);

    // ROI selection box (image coords -> widget coords, same transform as the image)
    if (!m_transientImage.isNull())
    {
        // A is still the editing/analysis target. Do not paint A's annotations
        // over B's transient raster or let the temporary view become editable.
    }
    else
        drawSelectionOverlay(p, m_selection, m_sourceSize, cx, cy, dw, dh);

    // M16.1: synced crosshair at the shared image-space position (n/n compare).
    if (m_transientImage.isNull() && m_crosshairOn)
        drawCrosshair(p, cx, cy, dw, dh);

    // M16.1: focus-lock highlight — draw a thick accent border when this cell
    // is the locked reference.
    if (m_transientImage.isNull() && m_focused)
    {
        QPen pen(QColor(0xFF, 0xB0, 0x20), 3);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, width() - 3.0, height() - 3.0));
    }

    // A-4.3: Pixel Link markers — numbered dots at image-space points.
    if (m_transientImage.isNull() && !m_linkMarkers.isEmpty() && m_scale > 0.0)
    {
        for (int i = 0; i < m_linkMarkers.size(); ++i)
        {
            const QPointF &pt = m_linkMarkers[i];
            const QPointF widgetPoint = sourcePointToWidget(pt);
            if (!std::isfinite(widgetPoint.x()) || !std::isfinite(widgetPoint.y()))
                continue;
            const double wx = widgetPoint.x();
            const double wy = widgetPoint.y();
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

void RawImageView::ensureBaseSurface()
{
    const qreal dpr = devicePixelRatioF();
    const QSize viewport = size();
    const QImage &image = displayImage();
    const QRect sourceRect = renderSourceRect();
    const qint64 imageKey = image.isNull() ? -1 : image.cacheKey();
    const qint64 overlayKey = m_overlay.isNull() ? -1 : m_overlay.cacheKey();

    // Cache key: image/overlay content (cheap unique buffer ids, never a pixel
    // compare), overlay opacity, scale, pan offset, viewport, and device ratio.
    if (m_baseSurfaceValid && imageKey == m_cachedImageKey && overlayKey == m_cachedOverlayKey &&
        m_overlayAlpha == m_cachedOverlayAlpha && m_scale == m_cachedScale &&
        m_offset == m_cachedOffset && viewport == m_cachedViewport && dpr == m_cachedDpr &&
        sourceRect == m_cachedSourceRect)
        return;

    // Bounded by widget viewport device pixels (never by scaled source dims:
    // 50x zoom still allocates viewport size). A defensive cap guards
    // pathological widget geometry; on any failure the caller falls back to the
    // direct draw path instead of showing a blank pane.
    const int w = qCeil(width() * dpr);
    const int h = qCeil(height() * dpr);
    const qint64 pixels = static_cast<qint64>(w) * h;
    if (w <= 0 || h <= 0 || w > kMaxCacheDim || h > kMaxCacheDim || pixels > kMaxSurfacePixels)
    {
        releaseBaseSurface();
        return;
    }

    // Reuse the existing allocation when its physical size, format, and DPR are
    // still compatible; reallocate only when the viewport geometry requires it,
    // so pan/zoom repaints never churn the heap. Clear and repaint in place.
    if (m_baseSurface.isNull() || m_baseSurface.width() != w || m_baseSurface.height() != h ||
        m_baseSurface.format() != QImage::Format_ARGB32_Premultiplied ||
        m_baseSurface.devicePixelRatio() != dpr)
    {
        m_baseSurface = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_baseSurface.setDevicePixelRatio(dpr);
        if (m_baseSurface.isNull())
        {
            releaseBaseSurface();
            return;
        }
    }

    m_baseSurface.fill(Qt::transparent);
    QPainter p(&m_baseSurface);
    drawBaseLayer(p);
    p.end();

    m_baseSurfaceValid = true;
    m_cachedImageKey = imageKey;
    m_cachedOverlayKey = overlayKey;
    m_cachedOverlayAlpha = m_overlayAlpha;
    m_cachedScale = m_scale;
    m_cachedOffset = m_offset;
    m_cachedViewport = viewport;
    m_cachedDpr = dpr;
    m_cachedSourceRect = sourceRect;

    ++m_baseSurfaceRenderCount;
    // Diagnostic only: lets tests distinguish annotation repaints from source
    // rasterization without widening the public API.
    setProperty("baseSurfaceRenderCount",
                QVariant::fromValue<qulonglong>(m_baseSurfaceRenderCount));
}

void RawImageView::drawBaseLayer(QPainter &p)
{
    // Single source of truth for the base image + diff overlay geometry shared
    // by the cached viewport surface and the direct-draw fallback. Matches the
    // pre-cache paintEvent rendering exactly.
    const QImage &image = displayImage();
    const QSize sourceSize = renderSourceSize();
    const QRect sourceRect = renderSourceRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 4.0);

    // Center in widget, then apply pan offset, then scale.
    const double cx = width() / 2.0 + m_offset.x();
    const double cy = height() / 2.0 + m_offset.y();
    const int dw = qRound(sourceSize.width() * m_scale);
    const int dh = qRound(sourceSize.height() * m_scale);
    const double sourceLeft = cx - dw / 2.0;
    const double sourceTop = cy - dh / 2.0;
    const QRectF coveredDest(sourceLeft + sourceRect.x() * m_scale,
                             sourceTop + sourceRect.y() * m_scale,
                             sourceRect.width() * m_scale,
                             sourceRect.height() * m_scale);
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

void RawImageView::releaseBaseSurface()
{
    m_baseSurface = QImage();
    m_baseSurfaceValid = false;
    m_cachedImageKey = -1;
    m_cachedOverlayKey = -1;
    m_cachedOverlayAlpha = -1.0;
    m_cachedScale = 0.0;
    m_cachedOffset = {};
    m_cachedViewport = {};
    m_cachedDpr = 0.0;
    m_cachedSourceRect = {};
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
        m_selection = mviewer::domain::normalizeSelection(
            m_selectStart.x(), m_selectStart.y(), cur.x(), cur.y(), m_sourceSize.width(),
            m_sourceSize.height());
        emit selectionPreviewChanged(m_selection);
        update();
        ev->accept();
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
            if (ix >= 0 && iy >= 0 && ix < m_sourceSize.width() && iy < m_sourceSize.height())
            {
                // The pane image is a bounded display LOD. It is intentionally
                // not an analysis source, so the legacy RGB payload is left
                // empty; CompareWorkspace re-samples the ImageFrame by (ix,iy).
                emit pixelInfo(ix, iy, 0, 0, 0, true);
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
        if (m_selectionMoved)
            emit selectionChanged(m_selection);
        ev->accept();
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
