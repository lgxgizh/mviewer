// CompareWorkspace rendering: paint modes, diff overlay, blink, histograms (M20 P0#2).
#include "compareworkspace_p.h"

void CompareWorkspace::refreshCellDiff(int idx)
{
    if (idx < 0 || idx >= m_cellViews.size())
        return;
    const int baseIdx = diffBaseIndex();
    RawImageView *view = m_cellViews[idx];
    // Clear any stale mismatch badge first; re-set below only when sizes differ.
    view->setSizeMismatch(false);
    if (idx == baseIdx)
    {
        view->setOverlay(QImage(), 0.0);
        return;
    }
    const ImageData basePx = adjustedPixels(baseIdx);
    const ImageData tgtPx = adjustedPixels(idx);
    if (basePx.isNull() || tgtPx.isNull())
    {
        view->setOverlay(QImage(), 0.0);
        return;
    }
    const auto bv = basePx.view();
    const auto tv = tgtPx.view();
    if (bv.width != tv.width || bv.height != tv.height)
    {
        view->setOverlay(QImage(), 0.0);
        return;
    }
    const ImageData diff = DifferenceEngine::differenceMap(tgtPx, basePx);
    if (diff.isNull())
        return;
    const ImageData thresholded = DifferenceEngine::applyThreshold(diff, m_thresholdValue);
    const ImageData overlayImg =
        m_diffHighlight ? DifferenceEngine::highlightMap(thresholded, basePx, m_thresholdValue)
                        : DifferenceEngine::heatMap(thresholded);
    if (overlayImg.isNull())
        return;
    view->setOverlay(mvcore::toQImage(overlayImg), m_diffHighlight ? 0.75 : 0.5);
}

void CompareWorkspace::refreshDiffOverlay()
{
    const auto res = m_engine.lastDiff();
    if (!res.valid || res.index < 0 || res.index >= m_cellViews.size())
    {
        for (int i = 0; i < m_cellViews.size(); ++i)
            refreshCellDiff(i);
        return;
    }
    refreshCellDiff(res.index);
    update();
}

void CompareWorkspace::toggleBlink()
{
    m_blinkState = !m_blinkState;
    applyBlink(m_blinkState);
    update();
}

// M15 P0#1: start the blink timer at the given interval (ms). Extracted so the
// persisted blink interval can be restored via applySession().
void CompareWorkspace::startBlink(int intervalMs)
{
    if (!m_blinkTimer)
    {
        m_blinkTimer = new QTimer(this);
        connect(m_blinkTimer, &QTimer::timeout, this, [this]() { this->toggleBlink(); });
    }
    m_blinkTimer->start(intervalMs);
    m_blinkState = false;
    applyBlink(m_blinkState);
}

void CompareWorkspace::stopBlink()
{
    if (m_blinkTimer)
        m_blinkTimer->stop();
    // restore all cells visible
    for (auto *v : m_cellViews)
        if (v)
            v->setVisible(true);
    // Rebuild the grid layout to restore proper cell positions after blink
    // may have repositioned cells.
    rebuildCells();
    fitAll();
    update();
}

void CompareWorkspace::applyBlink(bool state)
{
    const int n = m_cellViews.size();
    if (n == 0)
        return;

    // M1: honor the locked reference as the blink base. diffBaseIndex() returns 0
    // when nothing is locked, so this is a strict superset of the old behavior and
    // keeps blink consistent with the diff/inspector (which also use diffBaseIndex).
    const int base = qBound(0, diffBaseIndex(), n - 1);

    // For exactly two images, blink looks best when the active image fills the
    // entire grid area (rather than staying in its own cell slot). We achieve
    // this by showing only the active cell and stretching it across the grid.
    if (n == 2 && m_grid && m_layout)
    {
        const int other = (base == 0) ? 1 : 0;
        const int activeIdx = state ? other : base;
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            m_cellViews[i]->setVisible(i == activeIdx);
        }
        // Reposition the active cell to span the entire grid area.
        for (int i = 0; i < m_layout->count(); ++i)
        {
            QLayoutItem *item = m_layout->itemAt(i);
            if (item && item->widget())
            {
                m_layout->removeWidget(item->widget());
                --i;
            }
        }
        // Re-add the active cell's parent widget spanning the full grid.
        if (activeIdx < m_cellViews.size() && m_cellViews[activeIdx])
        {
            auto *cellWidget = m_cellViews[activeIdx]->parentWidget();
            if (cellWidget)
                m_layout->addWidget(cellWidget, 0, 0, -1, -1);
        }
    }
    else
    {
        // For 3+ images: toggle visibility of cell 0 vs all others.
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            if (i == base)
                m_cellViews[i]->setVisible(!state);
            else
                m_cellViews[i]->setVisible(state);
        }
    }
}

void CompareWorkspace::updateInspector(int x, int y)
{
    if (!m_inspector)
        return;
    const auto probe = m_engine.inspectPixel(x, y, diffBaseIndex());
    if (probe.samples.empty() || !probe.samples[0].valid)
    {
        m_inspector->setRowCount(0);
        return;
    }
    const int n = m_engine.imageCount();
    m_inspector->setRowCount(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        const QString name = img ? QString::fromStdString(img->metadata().fileName) : QString();
        if (static_cast<size_t>(i) >= probe.samples.size())
            continue;
        const auto &s = probe.samples[static_cast<size_t>(i)];
        const double dist = (static_cast<size_t>(i) < probe.deltas.size())
                                ? probe.deltas[static_cast<size_t>(i)].dist
                                : 0.0;
        m_inspector->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_inspector->setItem(i, 1, new QTableWidgetItem(name));
        m_inspector->setItem(i, 2, new QTableWidgetItem(QString::number(s.r)));
        m_inspector->setItem(i, 3, new QTableWidgetItem(QString::number(s.g)));
        m_inspector->setItem(i, 4, new QTableWidgetItem(QString::number(s.b)));
        m_inspector->setItem(i, 5, new QTableWidgetItem(QString::number(static_cast<int>(dist))));
    }
}

void CompareWorkspace::refreshHistograms()
{
    if (!m_hist)
        return;
    const int n = m_engine.imageCount();

    if (m_perPaneHist && m_editIdx >= 0 && m_editIdx < n)
    {
        // Per-pane: show only the selected cell's histogram
        const ImageFrame *img = m_engine.imageAt(m_editIdx);
        auto h = img ? mviewer::core::computeHistogram(img->pixels()) : mviewer::core::Histogram{};
        m_hist->setHistograms({h});
    }
    else
    {
        // Overlaid: show all
        std::vector<mviewer::core::Histogram> hists;
        hists.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const ImageFrame *img = m_engine.imageAt(i);
            hists.push_back(img ? mviewer::core::computeHistogram(img->pixels())
                                : mviewer::core::Histogram{});
        }
        m_hist->setHistograms(hists);
        // M16.7: keep the in-cell per-pane histograms in sync.
        for (int i = 0; i < static_cast<int>(m_cellHists.size()); ++i)
            refreshCellHist(i);
    }
}

void CompareWorkspace::paintEvent(QPaintEvent *)
{
    // Cells are raw QWidgets (RawImageView) that paint themselves. The workspace
    // only pushes the synchronized transform (scale + offset) so every cell tracks
    // the shared zoom/pan state. Image decode/overlay/draw lives in RawImageView,
    // never in this compare layer (see AGENTS.md: no decode in the QWidget layer).
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const auto &ct = m_engine.cellTransform(i);
        const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
        const QPointF off = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                                 m_engine.syncTransform().offset.y)
                                       : QPointF(ct.offset.x, ct.offset.y);
        m_cellViews[i]->setTransform(sc, off);
    }

    // P0-4: split / swipe overlay for two images.
    if (n == 2 && (isSplitOrSwipe() || (m_overlayChk && m_overlayChk->isChecked())) &&
        m_cellViews.size() >= 2)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_splitChk && m_splitChk->isChecked())
            drawSplitCompare(p);
        else if (m_swipeChk && m_swipeChk->isChecked())
            drawSwipeCompare(p, int(width() * m_splitPos));
        else if (m_overlayChk && m_overlayChk->isChecked())
            drawOverlayCompare(p);
    }

    // A-4.3: draw connecting lines between linked points across cells (2-up).
    if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && !m_linkPoints.isEmpty() && n >= 2)
    {
        QPainter p(this);
        drawPixelLinkLines(p);
    }
}

void CompareWorkspace::drawSplitCompare(QPainter &p)
{
    const QRect r = rect();
    const QRect left(r.topLeft(), QSize(r.width() / 2, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0),
                      QSize(r.width() - left.width() - 1, r.height()));
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();
    drawFitImage(p, img0, left);
    drawFitImage(p, img1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(left.topRight(), left.bottomRight());
}

void CompareWorkspace::drawSwipeCompare(QPainter &p, int x)
{
    const QRect r = rect();
    const QRect left(r.topLeft(), QSize(x, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0), QSize(r.width() - x, r.height()));
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();
    drawFitImage(p, img0, left);
    drawFitImage(p, img1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(x, 0), QPoint(x, r.height()));
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPoint(x, r.height() / 2), 4, 4);
}

// A-4.1: semi-transparent overlay blend of two images.
void CompareWorkspace::drawOverlayCompare(QPainter &p)
{
    if (m_cellViews.size() < 2)
        return;
    const QRect r = rect();
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();

    // Draw the base image fully opaque over the entire viewport.
    drawFitImage(p, img0, r);

    // Blend the second image on top with user-controlled opacity (A-4.1 slider).
    const double opacity = std::clamp(m_overlayAlpha / 100.0, 0.0, 1.0);
    p.setOpacity(opacity);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    drawFitImage(p, img1, r);
    p.setOpacity(1.0);
}

void CompareWorkspace::drawFitImage(QPainter &p, const QImage &img, const QRect &target)
{
    if (img.isNull() || target.width() <= 0 || target.height() <= 0)
        return;
    const QSizeF src(img.size());
    const QSizeF dst(target.size());
    const double s = qMin(dst.width() / src.width(), dst.height() / src.height());
    const int w = int(src.width() * s);
    const int h = int(src.height() * s);
    const QRect dr(target.x() + (target.width() - w) / 2, target.y() + (target.height() - h) / 2, w,
                   h);
    p.drawImage(dr, img);
}
