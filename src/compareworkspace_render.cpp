// CompareWorkspace rendering: paint modes, diff overlay, blink, histograms (M20 P0#2).
#include "compareworkspace_p.h"

void CompareWorkspace::rebuildCells()
{
    // The terminal refreshAllDiffOverlays() at the end of this function covers
    // the whole rebuild (including the ROI re-applied below), so suppress the
    // refresh that applySelectionToAll() would otherwise schedule.
    m_rebuildingCells = true;
    // Two-image Blink detaches the inactive pane so the active one can span the
    // grid. Reattach every tracked pane before draining the layout, otherwise a
    // rebuild triggered by layout/navigation would leave that pane orphaned.
    for (RawImageView *view : m_cellViews)
    {
        QWidget *pane = view ? view->parentWidget() : nullptr;
        if (pane && m_layout->indexOf(pane) < 0)
            m_layout->addWidget(pane);
    }

    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)))
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_cellLabels.clear();
    m_cellViews.clear();
    m_cellHists.clear();

    // Reset stale stretch factors from the previous layout. m_layout is reused
    // across rebuilds, and QGridLayout keeps row/column stretch even after the
    // widgets are removed — so switching e.g. 2x4 -> 1x2 would leave the empty
    // row/columns still claiming half of the viewport.
    for (int r = 0; r < m_layout->rowCount(); ++r)
        m_layout->setRowStretch(r, 0);
    for (int c = 0; c < m_layout->columnCount(); ++c)
        m_layout->setColumnStretch(c, 0);

    const int n = m_engine.imageCount();
    // Drop a stale focus lock when the image set shrank.
    if (m_focusIndex >= n)
    {
        m_focusIndex = -1;
        if (m_focusBtn)
            m_focusBtn->setChecked(false);
        if (m_focusLabel)
            m_focusLabel->setText(tr("基准: —"));
    }
    const auto &lay = m_engine.layout();
    for (int i = 0; i < n; ++i)
    {
        // Each cell: a RawImageView for the image + a QLabel caption below
        auto *cellWidget = new QWidget(m_grid);
        cellWidget->setObjectName(QString("comparePane%1").arg(i));
        auto *cellLay = new QVBoxLayout(cellWidget);
        cellLay->setContentsMargins(0, 0, 0, 0);
        cellLay->setSpacing(1);

        auto *view = new RawImageView(cellWidget);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        view->setMinimumSize(64, 64);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        view->setCellIndex(i);
        cellLay->addWidget(view, 1);
        m_cellViews.push_back(view);

        const ImageFrame *img = m_engine.imageAt(i);
        if (img && !img->pixels().isNull())
        {
            QImage q = imageObjectToQImage(img);
            view->setImage(q);
        }

        // M16.7: per-pane histogram overlay widget (hidden until toggled).
        {
            QFrame *hframe = new QFrame(cellWidget);
            hframe->setObjectName(QString("paneHistogramFrame%1").arg(i));
            hframe->setStyleSheet("background-color: rgba(15,15,15,210); border-radius:3px;");
            hframe->setVisible(m_paneHistOverlay);
            auto *hl = new QVBoxLayout(hframe);
            hl->setContentsMargins(2, 2, 2, 2);
            auto *hw = new HistogramWidget(hframe);
            hw->setObjectName(QString("paneHistogram%1").arg(i));
            hl->addWidget(hw);
            m_cellHists.push_back(hw);
        }

        const QString cellName = img ? QString::fromStdString(img->metadata().fileName) : QString();
        connect(view, &RawImageView::pixelInfo, this,
                [this, cellName](int x, int y, int r, int g, int b, bool valid)
                {
                    if (!valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    emit pixelInfo(QString("[%1] (%2,%3) RGB(%4,%5,%6)")
                                       .arg(cellName)
                                       .arg(x)
                                       .arg(y)
                                       .arg(r)
                                       .arg(g)
                                       .arg(b));
                    if (m_sidePanel && m_sidePanel->isVisible())
                    {
                        m_lastInspectX = x;
                        m_lastInspectY = y;
                        updateInspector(x, y);
                    }
                });
        connect(view, &RawImageView::selectionChanged, this,
                [this](const mviewer::domain::Selection &sel) { applySelectionToAll(sel); });
        connect(view, &RawImageView::crosshairMoved, this,
                [this, view](const QPointF &p) { onCrosshairMoved(view, p); });
        connect(view, &RawImageView::focusRequested, this, &CompareWorkspace::onFocusRequested);

        // A-4.3: restore any existing pixel-link markers after rebuild.
        if (!m_linkPoints.isEmpty())
            view->setLinkMarkers(m_linkPoints);

        // Caption label
        auto *caption = new QLabel(cellWidget);
        caption->setObjectName(QString("paneCaption%1").arg(i));
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("QLabel{background:#222;color:#ccc;padding:2px;}");
        caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        caption->setMinimumHeight(20);
        if (img)
            caption->setText(QString::fromStdString(img->metadata().fileName));
        cellLay->addWidget(caption);
        m_cellLabels.push_back(caption);

        const int row = i / lay.cols;
        const int col = i % lay.cols;
        m_layout->addWidget(cellWidget, row, col);
        // Let every cell expand to fill the available area so image panes (not the
        // widgets' minimum size) are what the grid lays out. Without stretch the
        // grid collapses to the cells' minimum size and fitAll() computes a tiny
        // shared scale, leaving compare panes blank or microscopic.
        m_layout->setRowStretch(row, 1);
        m_layout->setColumnStretch(col, 1);
    }

    // M3: a layout switch / swap / preset / blink-stop destroys and recreates every
    // cell view, which would silently drop the ROI the user drew. Re-apply the last
    // selection so the red box survives grid re-layouts (applySelectionToAll mirrors
    // it across all cells, exactly as when it was first drawn).
    if (m_lastSelection.width > 0)
        applySelectionToAll(m_lastSelection);

    // Rebuilds create fresh overlay widgets. Repopulate them immediately from
    // the same adjusted/ROI-aware histogram path used by the side panel.
    if (m_paneHistOverlay)
        for (int i = 0; i < static_cast<int>(m_cellHists.size()); ++i)
            refreshCellHist(i);

    if (m_blinkTimer && m_blinkTimer->isActive())
    {
        syncEngineBlink();
        applyBlink(m_blinkState);
    }

    // Every pane was recreated, so recompute all diff overlays + metrics in a
    // single async batch (latest-wins generation). This is the one terminal
    // refresh for the rebuild state transition.
    refreshAllDiffOverlays();
    m_rebuildingCells = false;

    QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
}

void CompareWorkspace::refreshAllDiffOverlays()
{
    const int paneCount = m_cellViews.size();

    // Latest-wins: cancel any in-flight batch and start a fresh generation.
    // Cancellation alone is not enough — a task may already be past its final
    // check when a newer request arrives, so the delivery is also guarded by
    // the generation (plus base index and pane count) on the UI thread.
    if (m_diffTask)
        TaskScheduler::cancel(m_diffTask);
    m_diffTask.reset();
    ++m_diffGen;

    // 0/1 panes: nothing to compare. Clear overlays and metrics synchronously
    // (cheap). For 2+ panes the previous target overlays stay visible while
    // the new batch is in flight.
    if (paneCount < 2)
    {
        for (RawImageView *view : m_cellViews)
        {
            if (!view)
                continue;
            view->setSizeMismatch(false);
            view->setOverlay(QImage(), 0.0);
        }
        if (m_metricLabel)
            m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        update();
        return;
    }

    const int baseIdx = std::clamp(diffBaseIndex(), 0, paneCount - 1);

    // Snapshot everything the worker needs BY VALUE. The worker only touches
    // these captures — no `this`, no QObject/QWidget. ImageData copies share
    // their pixel buffers, so the worker holds the pixels alive cheaply.
    std::vector<ImageData> pixels;
    pixels.reserve(paneCount);
    for (int i = 0; i < paneCount; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        pixels.push_back(img ? img->pixels() : ImageData());
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    const uint8_t threshold = m_thresholdValue;
    const bool highlight = m_diffHighlight;
    const mviewer::domain::Selection roi = m_lastSelection;
    const uint64_t gen = m_diffGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, adjusts, baseIdx, threshold, highlight, roi, paneCount, gen, guard](
            const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return; // superseded while queued — stop before any work

            const auto adjustFor = [&adjusts](int idx) -> CellAdjust
            {
                if (idx >= 0 && idx < static_cast<int>(adjusts.size()))
                    return adjusts[idx];
                return CellAdjust{};
            };

            DiffBatchResult r;
            r.generation = gen;
            r.baseIdx = baseIdx;

            // The base is adjusted exactly once and reused for every pane.
            const ImageData basePx =
                CompareWorkspace::applyAdjusts(pixels[baseIdx], adjustFor(baseIdx));
            if (ctx.isCancelled())
                return; // after base adjustment
            if (!basePx.isNull())
            {
                r.overlays.reserve(paneCount);
                int firstTarget = -1;
                // Adjusted target / difference map of the first non-base pane,
                // captured during the loop so each adjustment and diff runs
                // exactly once (shared between the overlay and the metrics).
                ImageData firstTgt;
                ImageData firstDiff;
                bool firstMismatch = false;
                for (int i = 0; i < paneCount; ++i)
                {
                    if (ctx.isCancelled())
                        return; // before target adjustment
                    DiffBatchResult::CellOverlay ov;
                    ov.index = i;
                    if (i == baseIdx)
                    {
                        r.overlays.push_back(std::move(ov));
                        continue;
                    }
                    if (firstTarget < 0)
                        firstTarget = i;

                    const ImageData tgtPx =
                        CompareWorkspace::applyAdjusts(pixels[i], adjustFor(i));
                    if (ctx.isCancelled())
                        return; // after target adjustment
                    if (tgtPx.isNull())
                    {
                        r.overlays.push_back(std::move(ov));
                        continue;
                    }
                    if (tgtPx.width != basePx.width || tgtPx.height != basePx.height)
                    {
                        ov.sizeMismatch = true;
                        if (firstTarget == i)
                        {
                            firstTgt = tgtPx;
                            firstMismatch = true;
                        }
                        r.overlays.push_back(std::move(ov));
                        continue;
                    }
                    const ImageData diff = DifferenceEngine::differenceMap(tgtPx, basePx);
                    if (ctx.isCancelled())
                        return; // after differenceMap
                    if (diff.isNull())
                    {
                        r.overlays.push_back(std::move(ov));
                        continue;
                    }
                    if (firstTarget == i)
                    {
                        firstTgt = tgtPx;
                        firstDiff = diff;
                    }
                    const ImageData thresholded = DifferenceEngine::applyThreshold(diff, threshold);
                    const ImageData overlayImg =
                        highlight ? DifferenceEngine::highlightMap(thresholded, basePx, threshold)
                                  : DifferenceEngine::heatMap(thresholded);
                    if (ctx.isCancelled())
                        return; // after threshold/map conversion
                    if (overlayImg.isNull())
                    {
                        r.overlays.push_back(std::move(ov));
                        continue;
                    }
                    ov.overlay = mvcore::toQImage(overlayImg);
                    ov.opacity = highlight ? 0.75 : 0.5;
                    r.overlays.push_back(std::move(ov));
                }

                // Metrics for the first non-base cell, mirroring updateMetrics().
                // Reuses the tgtPx/diff captured above. A null first target shows
                // generic empty metrics (never a size mismatch); only a valid
                // non-null target of unequal size reports one.
                if (firstTarget >= 0)
                {
                    r.targetIdx = firstTarget;
                    if (firstTgt.isNull())
                    {
                        // No usable target pixels: keep generic "PSNR/SSIM: —".
                    }
                    else if (firstMismatch)
                    {
                        r.sizeMismatch = true;
                    }
                    else if (!firstDiff.isNull())
                    {
                        if (ctx.isCancelled())
                            return; // before PSNR
                        r.psnr = AnalysisEngine::psnr(basePx, firstTgt);
                        if (ctx.isCancelled())
                            return; // before SSIM
                        r.ssim = AnalysisEngine::ssim(basePx, firstTgt);
                        r.metricsValid = true;

                        if (ctx.isCancelled())
                            return; // before full stats
                        r.stats = DifferenceEngine::computeStats(firstDiff, threshold);
                        r.hasStats = true;
                        if (!roi.isEmpty())
                        {
                            if (ctx.isCancelled())
                                return; // before ROI stats
                            r.roiStats = DifferenceEngine::computeStats(
                                firstDiff, threshold, roi.x, roi.y, roi.width, roi.height);
                            if (r.roiStats.totalPixels > 0)
                                r.hasRoiStats = true;
                        }
                        if (ctx.isCancelled())
                            return; // after stats
                    }
                }
            }
            else
            {
                // Base image unavailable: deliver empty overlays so the UI clears
                // stale state exactly like the synchronous path did.
                for (int i = 0; i < paneCount; ++i)
                {
                    DiffBatchResult::CellOverlay ov;
                    ov.index = i;
                    r.overlays.push_back(std::move(ov));
                }
            }

            if (ctx.isCancelled())
                return;

            // Marshal to the UI thread through qApp (outlives this workspace).
            // The queued lambda re-checks the guard AND the generation/base/pane
            // match before touching any widget.
            QMetaObject::invokeMethod(
                qApp,
                [guard, r]()
                {
                    CompareWorkspace *ws = guard.data();
                    if (!ws)
                        return;
                    ws->applyDiffBatchResult(r);
                },
                Qt::QueuedConnection);
        });
    if (!handle)
    {
        // submit() refused the task (pool paused / back-pressured). Leave
        // m_diffTask null and keep the last delivered overlay/metrics — never
        // fall back to synchronous compute on the UI thread. The generation
        // already advanced, so a later refresh supersedes this state.
        return;
    }
    m_diffTask = handle;
}

void CompareWorkspace::applyDiffBatchResult(const DiffBatchResult &r)
{
    if (r.generation != m_diffGen)
        return; // superseded by a newer batch
    if (r.baseIdx != diffBaseIndex())
        return; // the base reference changed while the batch was in flight
    if (r.overlays.size() != m_cellViews.size())
        return; // the pane layout changed while the batch was in flight

    // This is the current generation's terminal delivery: release the handle.
    m_diffTask.reset();

    for (const auto &ov : r.overlays)
    {
        if (ov.index < 0 || ov.index >= m_cellViews.size())
            continue;
        RawImageView *view = m_cellViews[ov.index];
        if (!view)
            continue;
        view->setSizeMismatch(ov.sizeMismatch);
        view->setOverlay(ov.overlay, ov.opacity);
    }

    if (m_metricLabel)
    {
        QString text;
        if (!r.metricsValid)
        {
            text = r.sizeMismatch ? tr("PSNR: —  SSIM: —\n(图像尺寸不一致)")
                                  : tr("PSNR: —  SSIM: —");
        }
        else
        {
            const QString psnrStr = QString::number(r.psnr, 'f', 2) + " dB";
            const QString ssimStr = QString::number(r.ssim, 'f', 4);
            text = tr("PSNR: %1  SSIM: %2\n(Image #%3 vs #%4)")
                       .arg(psnrStr, ssimStr)
                       .arg(r.baseIdx + 1)
                       .arg(r.targetIdx + 1);
            if (r.hasStats)
            {
                text += tr("\n差异: %1%  均值 %2  峰值 %3")
                            .arg(r.stats.diffRatio * 100.0, 0, 'f', 2)
                            .arg(r.stats.meanDiff, 0, 'f', 2)
                            .arg(r.stats.maxDiff);
                if (r.hasRoiStats)
                    text += tr("\nROI差异: %1%  均值 %2  峰值 %3")
                                .arg(r.roiStats.diffRatio * 100.0, 0, 'f', 2)
                                .arg(r.roiStats.meanDiff, 0, 'f', 2)
                                .arg(r.roiStats.maxDiff);
            }
        }
        m_metricLabel->setText(text);
    }
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
    syncEngineBlink();
}

void CompareWorkspace::stopBlink()
{
    if (m_blinkTimer)
        m_blinkTimer->stop();
    m_engine.clearBlink(); // M24: capture must report "blink off" after stopping

    // Restore visibility before rebuilding the normal grid. rebuildCells()
    // centrally reattaches any pane detached by two-image Blink.
    for (auto *v : m_cellViews)
    {
        if (!v)
            continue;
        QWidget *pane = v->parentWidget();
        if (!pane)
            continue;
        pane->setVisible(true);
    }
    // Rebuild the grid layout to restore proper cell positions after blink
    // may have repositioned cells.
    rebuildCells();
    fitAll();
    update();
}

// M24: drive the engine's BlinkController from the workspace blink state. The
// engine is the single source for CompareEngine::session(), so without this the
// persisted CompareSession never carried the blink target (always -1).
void CompareWorkspace::syncEngineBlink()
{
    const int n = m_engine.imageCount();
    if (n >= 2)
    {
        const int base = qBound(0, diffBaseIndex(), n - 1);
        m_engine.setBlinkIndex(base == 0 ? 1 : 0);
    }
    else
    {
        m_engine.clearBlink();
    }
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
            QWidget *pane = m_cellViews[i]->parentWidget();
            if (pane)
                pane->setVisible(i == activeIdx);
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
            {
                cellWidget->setVisible(true);
                m_layout->addWidget(cellWidget, 0, 0, -1, -1);
            }
        }
        QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
    }
    else
    {
        // For 3+ images: toggle visibility of cell 0 vs all others.
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            QWidget *pane = m_cellViews[i]->parentWidget();
            if (!pane)
                continue;
            if (i == base)
                pane->setVisible(!state);
            else
                pane->setVisible(state);
        }
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

    // P0-4 / A-4.1 / M23: split / swipe / overlay / checkerboard for two images.
    if (n == 2 && anyCanvasCompareMode() && m_cellViews.size() >= 2)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_splitChk && m_splitChk->isChecked())
            drawSplitCompare(p);
        else if (m_swipeChk && m_swipeChk->isChecked())
            drawSwipeCompare(p, int(width() * m_splitPos));
        else if (m_overlayChk && m_overlayChk->isChecked())
            drawOverlayCompare(p);
        else if (m_checkerChk && m_checkerChk->isChecked())
            drawCheckerboardCompare(p);
    }

    // A-4.3: draw connecting lines between linked points across cells (2-up).
    if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && !m_linkPoints.isEmpty() && n >= 2)
    {
        QPainter p(this);
        drawPixelLinkLines(p);
    }
}

void CompareWorkspace::drawCellCompare(QPainter &p, int idx, const QRect &rect)
{
    if (idx < 0 || idx >= m_cellViews.size() || !m_cellViews[idx])
        return;
    const QImage &img = m_cellViews[idx]->image();
    if (img.isNull() || rect.isEmpty())
        return;

    // H3: render with the SAME synchronized transform as Normal mode (scale + offset)
    // so the user's zoom/pan carries over into split/swipe/overlay. The old code
    // re-fit the image into the half, discarding the zoom/pan entirely. The offset
    // is measured from the rect's own origin, exactly like RawImageView does for a
    // cell widget.
    const auto &ct = m_engine.cellTransform(idx);
    const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
    const double ox = m_syncDrag ? m_engine.syncTransform().offset.x : ct.offset.x;
    const double oy = m_syncDrag ? m_engine.syncTransform().offset.y : ct.offset.y;

    p.save();
    p.setClipRect(rect);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF dr(rect.x() + ox, rect.y() + oy, img.width() * sc, img.height() * sc);
    p.drawImage(dr, img);

    // Diff/heatmap overlay (set per cell by the async batch result). Only the
    // non-base cell carries one, so this is a no-op for the base image —
    // preserving the diff view the engineer had in Normal mode.
    const QImage &ov = m_cellViews[idx]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[idx]->overlayOpacity());
        p.drawImage(dr, ov);
        p.setOpacity(1.0);
    }
    p.restore();
}

void CompareWorkspace::drawSplitCompare(QPainter &p)
{
    const QRect r = rect();
    const int midX = r.width() / 2;
    const QRect left(r.topLeft(), QSize(midX, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0), QSize(r.width() - midX - 1, r.height()));
    drawCellCompare(p, 0, left);
    drawCellCompare(p, 1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(left.topRight(), left.bottomRight());
}

void CompareWorkspace::drawSwipeCompare(QPainter &p, int x)
{
    const QRect r = rect();
    const QRect left(r.topLeft(), QSize(x, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0), QSize(r.width() - x, r.height()));
    drawCellCompare(p, 0, left);
    drawCellCompare(p, 1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(x, 0), QPoint(x, r.height()));
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPoint(x, r.height() / 2), 4, 4);
}

// A-4.1 + H3: semi-transparent overlay blend of two images, drawn with the
// synchronized transform, and with the diff overlay kept on top (the old blend
// re-fit the images and dropped the diff overlay).
void CompareWorkspace::drawOverlayCompare(QPainter &p)
{
    if (m_cellViews.size() < 2)
        return;
    const QRect r = rect();

    // Base image (full opacity), drawn with the synchronized transform.
    drawCellCompare(p, 0, r);

    const QImage &img1 = m_cellViews[1]->image();
    if (img1.isNull())
        return;
    const auto &ct = m_engine.cellTransform(1);
    const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
    const double ox = m_syncDrag ? m_engine.syncTransform().offset.x : ct.offset.x;
    const double oy = m_syncDrag ? m_engine.syncTransform().offset.y : ct.offset.y;
    const QRectF dr(r.x() + ox, r.y() + oy, img1.width() * sc, img1.height() * sc);

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // Blend the second image on top with user-controlled opacity (A-4.1 slider).
    p.setOpacity(std::clamp(m_overlayAlpha / 100.0, 0.0, 1.0));
    p.drawImage(dr, img1);
    p.setOpacity(1.0);
    // Keep the diff overlay visible on top of the blend (H3 fix).
    const QImage &ov = m_cellViews[1]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[1]->overlayOpacity());
        p.drawImage(dr, ov);
    }
    p.restore();
}

// ── M23: checkerboard compare (棋盘格) ──────────────────────────────────────
// Alternating blocks show image A and image B under the SAME synchronized
// transform, so block seams reveal misalignment/color shifts instantly.

void CompareWorkspace::buildCheckerboardControls(QHBoxLayout *lay)
{
    if (!lay)
        return;
    m_checkerChk = new QCheckBox(tr("棋盘对比(&K)"), this);
    m_checkerChk->setEnabled(false);
    m_checkerChk->setToolTip(tr("棋盘格交替显示两张图像（快捷键 K），块大小可调"));
    connect(m_checkerChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_checkerChk);
                if (m_grid)
                    m_grid->setVisible(!anyCanvasCompareMode());
                if (m_checkerSizeSlider)
                    m_checkerSizeSlider->setEnabled(on);
                update();
            });
    lay->addWidget(m_checkerChk);

    m_checkerSizeSlider = new QSlider(Qt::Horizontal, this);
    m_checkerSizeSlider->setRange(16, 256);
    m_checkerSizeSlider->setValue(m_checkerSize);
    m_checkerSizeSlider->setFixedWidth(70);
    m_checkerSizeSlider->setEnabled(false);
    m_checkerSizeSlider->setToolTip(tr("棋盘块边长（像素）"));
    connect(m_checkerSizeSlider, &QSlider::valueChanged, this,
            [this](int v)
            {
                m_checkerSize = v;
                if (m_checkerSizeLabel)
                    m_checkerSizeLabel->setText(QString("%1px").arg(v));
                if (m_checkerChk && m_checkerChk->isChecked())
                    update();
            });
    lay->addWidget(m_checkerSizeSlider);
    m_checkerSizeLabel = new QLabel(QString("%1px").arg(m_checkerSize), this);
    m_checkerSizeLabel->setMinimumWidth(34);
    lay->addWidget(m_checkerSizeLabel);
}

void CompareWorkspace::drawCheckerboardCompare(QPainter &p)
{
    if (m_cellViews.size() < 2 || !m_cellViews[0] || !m_cellViews[1])
        return;
    const QRect r = rect();

    // Base image fills the canvas with the synchronized transform.
    drawCellCompare(p, 0, r);

    const QImage &img1 = m_cellViews[1]->image();
    if (img1.isNull())
        return;

    // Build the clip region of "B" blocks: every block where (bx+by) is odd.
    const int bs = std::max(4, m_checkerSize);
    QRegion region;
    for (int by = 0, y = 0; y < r.height(); ++by, y += bs)
        for (int bx = 0, x = 0; x < r.width(); ++bx, x += bs)
            if (((bx + by) & 1) != 0)
                region += QRect(x, y, bs, bs).intersected(r);

    const auto &ct = m_engine.cellTransform(1);
    const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
    const double ox = m_syncDrag ? m_engine.syncTransform().offset.x : ct.offset.x;
    const double oy = m_syncDrag ? m_engine.syncTransform().offset.y : ct.offset.y;
    const QRectF dr(r.x() + ox, r.y() + oy, img1.width() * sc, img1.height() * sc);

    p.save();
    p.setClipRegion(region);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(dr, img1);
    // Keep the diff overlay visible inside the B blocks.
    const QImage &ov = m_cellViews[1]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[1]->overlayOpacity());
        p.drawImage(dr, ov);
        p.setOpacity(1.0);
    }
    p.restore();

    // Subtle block grid so the user can tell which region belongs to B.
    p.save();
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    for (int x = bs; x < r.width(); x += bs)
        p.drawLine(x, 0, x, r.height());
    for (int y = bs; y < r.height(); y += bs)
        p.drawLine(0, y, r.width(), y);
    p.restore();
}
