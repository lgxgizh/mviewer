// Canvas paint entry and overlay painters (pixel grid, ROI, filename and
// histogram), split out of compareworkspace_render_materialization.cpp.
#include "compareworkspace_p.h"

#include "widgets/infooverlay.h"
#include "widgets/pixelgrid.h"
#include "widgets/roioverlay.h"

#include <QDataStream>
#include <QVariant>
#include <QtMath>

namespace
{
// Same dual-tone divider drawSplitCompare paints at the halves' boundary.
void drawSplitDivider(QPainter &p, const QRect &r, int x)
{
    p.setPen(QPen(QColor(0, 0, 0, 110), 3));
    p.drawLine(QPoint(x, r.top()), QPoint(x, r.bottom()));
    p.setPen(QPen(QColor(255, 255, 255, 230), 1));
    p.drawLine(QPoint(x, r.top()), QPoint(x, r.bottom()));
}
} // namespace

// Canvas paint entry — invoked from the canvas event path (Paint event via the
// workspace event filter). Renders with the canvas rect only, never the parent.
void CompareWorkspace::paintCompareCanvas()
{
    QWidget *cv = m_compareCanvas;
    if (!cv)
        return;
    const QRect r = cv->rect();
    if (m_engine.imageCount() != 2 || m_cellViews.size() < 2 || !anyCanvasCompareMode())
        return;
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << cv->size() << cv->devicePixelRatioF() << m_splitPos << m_overlayAlpha << m_checkerSize
           << static_cast<int>(m_displayOverlay) << (m_splitChk && m_splitChk->isChecked())
           << (m_swipeChk && m_swipeChk->isChecked()) << (m_overlayChk && m_overlayChk->isChecked())
           << (m_checkerChk && m_checkerChk->isChecked()) << m_canvasTemporaryPane;
    for (int index = 0; index < 2; ++index)
    {
        const RawImageView *view = m_cellViews[index];
        const auto &transform = m_engine.cellTransform(index);
        stream << view->presentationImage().cacheKey() << view->overlay().cacheKey()
               << view->sourceSize() << view->sourceRect() << view->overlayOpacity()
               << transform.scale << transform.offset.x << transform.offset.y;
    }
    stream << m_engine.syncTransform().offset.x << m_engine.syncTransform().offset.y << m_syncDrag
           << m_syncZoom << m_uniformScale << m_sharedZoomRatio;

    const qreal dpr = cv->devicePixelRatioF();
    const QSize physical(qCeil(cv->width() * dpr), qCeil(cv->height() * dpr));
    if (m_canvasBaseKey != key || m_canvasBaseSurface.isNull() ||
        m_canvasBaseSurface.size() != physical)
    {
        m_canvasBaseSurface = QImage(physical, QImage::Format_ARGB32_Premultiplied);
        m_canvasBaseSurface.setDevicePixelRatio(dpr);
        m_canvasBaseSurface.fill(palette().color(QPalette::Dark));
        QPainter base(&m_canvasBaseSurface);
        base.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (m_canvasTemporaryPane >= 0)
        {
            // L6: in Split, Space must stay in the hovered half — not full canvas
            // (full-canvas was a 2x jump of center and scale). The other half
            // keeps showing its own image instead of going blank.
            if (m_splitChk && m_splitChk->isChecked())
            {
                for (int slot = 0; slot < 2; ++slot)
                {
                    const QRectF half = canvasPaneGeometry(slot);
                    drawCellCompare(base, displayedImageIndex(slot), half.toRect().intersected(r),
                                    half);
                }
                drawSplitDivider(base, r, canvasPaneGeometry(1).toRect().left());
            }
            else
            {
                drawCellCompare(base, m_canvasTemporaryPane, r, QRectF(r));
            }
        }
        else if (m_splitChk && m_splitChk->isChecked())
            drawSplitCompare(base);
        else if (m_swipeChk && m_swipeChk->isChecked())
            drawSwipeCompare(base, int(r.width() * m_splitPos));
        else if (m_overlayChk && m_overlayChk->isChecked())
            drawOverlayCompare(base);
        else if (m_checkerChk && m_checkerChk->isChecked())
            drawCheckerboardCompare(base);
        base.end();
        m_canvasBaseKey = key;
        ++m_canvasBaseRenderCount;
        cv->setProperty("baseSurfaceRenderCount",
                        QVariant::fromValue<qulonglong>(m_canvasBaseRenderCount));
    }
    QPainter p(cv);
    p.drawImage(QPoint(0, 0), m_canvasBaseSurface);
    drawCanvasPixelGrid(p);
    drawCanvasROI(p);
    drawCanvasInfoOverlays(p);
}

void CompareWorkspace::drawCanvasROI(QPainter &p)
{
    if (m_lastSelection.isEmpty() || !m_roiLinked)
        return;
    // Each slot outlines the ROI on the image it shows (a held Space / 1-8
    // switch draws the other image there, with that image's geometry and tag).
    const auto drawPane = [this, &p](int slot)
    {
        const int pane = displayedImageIndex(slot);
        if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
            return;
        mviewer::ui::drawROIOverlay(p, m_lastSelection, m_cellViews[pane]->sourceSize(),
                                    cellFullDestRect(pane, canvasPaneGeometry(slot)), true,
                                    QString(QChar('A' + pane)));
    };
    if (m_splitChk && m_splitChk->isChecked())
    {
        drawPane(0);
        drawPane(1);
    }
    else
        drawPane(0);
}

void CompareWorkspace::drawCanvasPixelGrid(QPainter &p)
{
    if (!m_pixelGridEnabled || !m_compareCanvas || m_cellViews.size() < 2)
        return;
    const auto drawPane = [this, &p](int pane)
    {
        if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
            return;
        const RawImageView *view = m_cellViews[pane];
        const QSize sourceSize = view->sourceSize();
        if (!sourceSize.isValid() || sourceSize.width() <= 0 || sourceSize.height() <= 0)
            return;
        // Same gate as RawImageView: no lattice over a soft / lower-density raster.
        if (!mviewer::pixelGridMatchesRaster(view->image().width(), view->image().height(),
                                             view->sourceRect().width(),
                                             view->sourceRect().height(), view->softLoading()))
            return;
        const QRectF geom = canvasPaneGeometry(pane);
        const QRectF dest = cellFullDestRect(pane, geom);
        mviewer::ui::drawPixelGrid(p, dest, 0, 0, sourceSize.width(), sourceSize.height(), geom);
    };
    if (m_splitChk && m_splitChk->isChecked())
    {
        drawPane(0);
        drawPane(1);
    }
    else
        drawPane(0);
}

void CompareWorkspace::drawCanvasInfoOverlays(QPainter &p)
{
    if (!m_compareCanvas || m_cellViews.size() < 2)
        return;
    if (!m_filenameOverlay && !m_paneHistOverlay)
        return;
    const auto drawPane = [this, &p](int pane)
    {
        if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
            return;
        const QRect geom = canvasPaneGeometry(pane).toRect();
        QRect filenameBox;
        if (m_filenameOverlay)
            filenameBox =
                mviewer::ui::drawFilenameOverlay(p, geom, m_cellViews[pane]->filenameOverlayText());
        if (!m_paneHistOverlay || pane >= static_cast<int>(m_cellHists.size()) ||
            !m_cellHists[pane])
            return;
        const QRect histBox = mviewer::ui::histogramOverlayRect(geom, filenameBox);
        if (histBox.isEmpty())
            return;
        m_cellHists[pane]->paintOverlay(p, histBox);
    };
    if (m_splitChk && m_splitChk->isChecked())
    {
        drawPane(0);
        drawPane(1);
    }
    else
        drawPane(0);
}
