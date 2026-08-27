#include "compareworkspace_p.h"

#include "core/image/SourceImage.h"

#include <algorithm>
#include <cmath>
#include <QResizeEvent>

namespace
{
class ElidedCaption final : public QLabel
{
  public:
    explicit ElidedCaption(QWidget *parent) : QLabel(parent) {}

    void setFullText(const QString &text)
    {
        m_fullText = text;
        setToolTip(text);
        updateText();
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

  private:
    void updateText()
    {
        const int available = std::max(0, contentsRect().width() - 8);
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideMiddle, available));
    }

    QString m_fullText;
};
}
void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    const bool normalGrid = m_pageStack && m_compareGridPage &&
                            m_pageStack->currentWidget() == m_compareGridPage;
    if (!blinkActive && normalGrid)
    {
        // Coalesce ordinary-grid resize bursts through the same post-layout
        // path used by mode transitions. It refits Fit-state panes to the new
        // geometry while preserving an intentional shared relative zoom ratio.
        schedulePostLayoutFit();
    }
    else
    {
        // Canvas, Blink, and loading-page resize behavior remains the legacy
        // immediate fit path; the scheduled grid callback intentionally skips
        // those hidden or detached panes.
        fitAll();
    }
    positionCellHists();
    if (blinkActive || !normalGrid)
        scheduleDisplayLodRefresh();
}

// Canvas paint entry — invoked from the canvas event path (Paint event via the
// workspace event filter). Renders with the canvas rect only, never the parent.
void CompareWorkspace::paintCompareCanvas()
{
    QWidget *cv = m_compareCanvas;
    if (!cv)
        return;
    QPainter p(cv);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect r = cv->rect();
    p.fillRect(r, palette().color(QPalette::Dark));
    if (m_engine.imageCount() != 2 || m_cellViews.size() < 2 || !anyCanvasCompareMode())
        return;
    if (m_splitChk && m_splitChk->isChecked())
        drawSplitCompare(p);
    else if (m_swipeChk && m_swipeChk->isChecked())
        drawSwipeCompare(p, int(r.width() * m_splitPos));
    else if (m_overlayChk && m_overlayChk->isChecked())
        drawOverlayCompare(p);
    else if (m_checkerChk && m_checkerChk->isChecked())
        drawCheckerboardCompare(p);
}

void CompareWorkspace::rebuildCells()
{
    endTemporaryCompare();
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
    buildCompareCells(n, m_engine.layout().cols);

    // M3: a layout switch / swap / preset / blink-stop destroys and recreates every
    // cell view, which would silently drop the ROI the user drew. Re-apply the last
    // selection so the red box survives grid re-layouts (applySelectionToAll mirrors
    // it across all cells, exactly as when it was first drawn).
    if (m_lastSelection.width > 0)
        applySelectionToAll(m_lastSelection);

    // Rebuilds create fresh overlay widgets. Request every pane histogram in
    // ONE async batch from the same adjusted/ROI-aware path used by the side
    // panel — never an N-call loop.
    if (m_paneHistOverlay)
    {
        std::vector<int> all;
        const int n = static_cast<int>(m_cellHists.size());
        all.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            all.push_back(i);
        scheduleHistogramRefresh(false, all);
    }

    if (m_blinkTimer && m_blinkTimer->isActive())
    {
        syncEngineBlink();
        applyBlink(m_blinkState);
    }

    // M28 P1-01: every pane was recreated, so materialize all pane images in ONE
    // async display batch (latest-wins generation). This is independent of the
    // terminal diff batch below: each rebuild submits exactly two Analysis tasks.
    {
        std::vector<int> all;
        all.reserve(m_cellViews.size());
        for (int i = 0; i < static_cast<int>(m_cellViews.size()); ++i)
            all.push_back(i);
        scheduleDisplayMaterialization(all);
    }

    // Every pane was recreated, so recompute all diff overlays + metrics in a
    // single async batch (latest-wins generation). This is the one terminal
    // refresh for the rebuild state transition.
    refreshAllDiffOverlays();
    m_rebuildingCells = false;

    QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
}

// M28 P1-01: schedule an async pane-materialization batch for the given panes
// (plus every pane currently showing a null image, so canceling an initial
// all-pane batch with a later single-pane adjustment never strands blank
// panes). One Analysis-priority task per call; never a synchronous fallback.
void CompareWorkspace::buildCompareCells(int n, int columns)
{
    for (int i = 0; i < n; ++i)
    {
        // Each cell: a RawImageView for the image + a QLabel caption below
        auto *cellWidget = new QWidget(m_grid);
        cellWidget->setObjectName(QString("comparePane%1").arg(i));
        cellWidget->setMinimumSize(0, 0);
        cellWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
        connect(view, &RawImageView::scaleChanged, this,
                [this, view](double) { scheduleDisplayLodRefresh(view->cellIndex()); });

        // M28 P1-01: panes start BLANK. ImageData -> QImage materialization is
        // one async Analysis batch scheduled below, never a synchronous
        // conversion on the UI thread.
        const ImageFrame *img = m_engine.imageAt(i);

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
                [this, cellName, cellIndex = i](int x, int y, int, int, int, bool valid)
                {
                    if (!valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    const ImageFrame *frame = m_engine.imageAt(cellIndex);
                    const CellAdjust adjust =
                        cellIndex >= 0 && cellIndex < static_cast<int>(m_cellAdjusts.size())
                            ? m_cellAdjusts[static_cast<size_t>(cellIndex)]
                            : CellAdjust{};
                    const auto sample = frame
                                            ? mviewer::core::sampleAnalysisPixel(
                                                  frame->pixels(), analysisAdjustment(adjust), x, y)
                                            : mviewer::core::AnalysisPixel{};
                    if (!sample.valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    emit pixelInfo(QString("[%1] (%2,%3) RGB(%4,%5,%6)")
                                       .arg(cellName)
                                       .arg(x)
                                       .arg(y)
                                       .arg(sample.r)
                                       .arg(sample.g)
                                       .arg(sample.b));
                    // M30: route the high-frequency hover through the coalescer
                    // so the sync-crosshair pixelInfo + crosshairMoved pair and
                    // rapid hovers render the inspector at most once per turn.
                    if (m_sidePanel && m_sidePanel->isVisible())
                        requestInspectorUpdate(x, y);
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
        auto *caption = new ElidedCaption(cellWidget);
        caption->setObjectName(QString("paneCaption%1").arg(i));
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("QLabel{background:#222;color:#ccc;padding:2px;}");
        caption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        caption->setMinimumWidth(0);
        caption->setMinimumHeight(20);
        if (img)
            caption->setFullText(QString::fromUtf8(img->metadata().fileName.data(),
                                                   static_cast<int>(img->metadata().fileName.size())));
        cellLay->addWidget(caption);
        m_cellLabels.push_back(caption);

        const int row = i / columns;
        const int col = i % columns;
        m_layout->addWidget(cellWidget, row, col);
        // Let every cell expand to fill the available area so image panes (not the
        // widgets' minimum size) are what the grid lays out. Without stretch the
        // grid collapses to the cells' minimum size and fitAll() computes a tiny
        // shared scale, leaving compare panes blank or microscopic.
        m_layout->setRowStretch(row, 1);
        m_layout->setColumnStretch(col, 1);
    }

}

TaskScheduler::TaskHandle CompareWorkspace::startDisplayMaterialization(
    const std::vector<ImageData> &pixels, const std::vector<mviewer::domain::ImageMetadata> &metadata,
    const std::vector<DisplayRequest> &displayRequests, const std::vector<CellAdjust> &adjusts,
    const std::vector<int> &panes, int paneCount, uint64_t gen,
    const std::vector<std::string> &paths, const QPointer<CompareWorkspace> &guard)
{
    return TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, metadata, displayRequests, adjusts, panes, paneCount, gen, paths, guard](
            const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return; // superseded while queued — stop before any work

            DisplayBatchResult r;
            r.generation = gen;
            r.paneCount = paneCount;
            r.cells.reserve(panes.size());

            const auto adjustFor = [&adjusts](int idx) -> CellAdjust
            {
                if (idx >= 0 && idx < static_cast<int>(adjusts.size()))
                    return adjusts[idx];
                return CellAdjust{};
            };

            for (int idx : panes)
            {
                if (ctx.isCancelled())
                    return; // before pane materialization
                if (idx < 0 || idx >= static_cast<int>(pixels.size()))
                    continue;
                const ImageData &src = pixels[static_cast<size_t>(idx)];
                QSize sourceDims;
                mviewer::domain::ImageMetadata convMeta;
                ImageData lod;
                QRect coveredRect;
                const DisplayRequest request =
                    idx < static_cast<int>(displayRequests.size()) ? displayRequests[idx]
                                                                     : DisplayRequest{};
                if (!src.isNull())
                {
                    // Full frame available: bounded client-side display LOD
                    // from the frame pixels (existing path).
                    sourceDims = QSize(src.width, src.height);
                    convMeta = metadata[static_cast<size_t>(idx)];
                    const QSize target = request.target;
                    lod = target.isValid()
                              ? RenderEngine::scaleBoundedStatic(
                                    src, RenderSize{target.width(), target.height()})
                              : ImageData();
                    coveredRect = QRect(QPoint(0, 0), sourceDims);
                }
                else
                {
                    // M47: source-backed display — bounded viewport LOD from
                    // the source (native where available, e.g. JPEG). The pane
                    // never waits for a full frame; the full frame, when
                    // loaded, remains the analysis source. M48 Phase 1: the
                    // decode returns an atomic pixels+metadata result, so the
                    // display conversion uses the authoritative metadata AS OF
                    // this decode (ICC included), never a pre-decode snapshot.
                    if (idx >= static_cast<int>(paths.size()) ||
                        paths[static_cast<size_t>(idx)].empty())
                        continue;
                    auto source = mviewer::core::SourceImage::open(
                        paths[static_cast<size_t>(idx)]);
                    if (!source)
                        continue;
                    sourceDims = QSize(source->metadata().width, source->metadata().height);
                    if (sourceDims.isEmpty())
                        continue;
                    const QSize target = request.target;
                    mviewer::core::SourceImage::RasterResult result;
                    if (request.region && request.sourceRect.isValid())
                    {
                        const mviewer::core::SourceRect displayed{
                            request.sourceRect.x(), request.sourceRect.y(),
                            request.sourceRect.width(), request.sourceRect.height()};
                        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
                            displayed, source->rawWidth(), source->rawHeight(),
                            source->orientation());
                        result = source->decodeRegion(raw, target.width(), target.height());
                        const mviewer::core::SourceRect displayedCovered =
                            mviewer::core::rawRectToOriented(result.coveredRect, source->rawWidth(),
                                                             source->rawHeight(),
                                                             source->orientation());
                        coveredRect = QRect(displayedCovered.x, displayedCovered.y,
                                            displayedCovered.w, displayedCovered.h);
                    }
                    else
                    {
                        const int edge = std::max(target.width(), target.height());
                        result = source->decodeLod(edge > 0 ? edge : 1024);
                        coveredRect = QRect(QPoint(0, 0), sourceDims);
                    }
                    if (!result.ok)
                        continue;
                    lod = result.pixels;
                    convMeta = result.metadata;
                }
                if (ctx.isCancelled())
                    return; // after bounded scale
                if (lod.isNull())
                    continue;
                if (sourceDims.isEmpty())
                    sourceDims = QSize(lod.width, lod.height);
                if (!coveredRect.isValid())
                    coveredRect = QRect(QPoint(0, 0), sourceDims);

                // This is a bounded visual-preview approximation: nonlinear
                // point operations (especially gamma and clipping) do not
                // strictly commute with downsampling. Analysis never consumes
                // this LOD; it maps the hover coordinate back to source pixels
                // and applies point-wise adjustments without a full-res QImage.
                CellAdjust displayAdjust = adjustFor(idx);
                if (displayAdjust.hasCrop && sourceDims.width() > 0 && sourceDims.height() > 0)
                {
                    const double sx = static_cast<double>(lod.width) / sourceDims.width();
                    const double sy = static_cast<double>(lod.height) / sourceDims.height();
                    displayAdjust.cropX = static_cast<int>(std::lround(displayAdjust.cropX * sx));
                    displayAdjust.cropY = static_cast<int>(std::lround(displayAdjust.cropY * sy));
                    displayAdjust.cropW = static_cast<int>(std::lround(displayAdjust.cropW * sx));
                    displayAdjust.cropH = static_cast<int>(std::lround(displayAdjust.cropH * sy));
                }
                const ImageData adjusted = CompareWorkspace::applyAdjusts(lod, displayAdjust);
                if (ctx.isCancelled())
                    return; // after bounded adjustment
                if (adjusted.isNull())
                    continue; // failed adjustment must not clear the last valid preview
                DisplayBatchResult::CellImage cell;
                cell.index = idx;
                cell.image = mvcore::toDisplayQImage(adjusted, convMeta);
                cell.sourceSize = (displayAdjust.hasCrop || displayAdjust.rotation != 0)
                                      ? QSize(adjusted.width, adjusted.height)
                                      : sourceDims;
                cell.sourceRect = (displayAdjust.hasCrop || displayAdjust.rotation != 0)
                                      ? QRect(QPoint(0, 0), cell.sourceSize)
                                      : coveredRect;
                if (ctx.isCancelled())
                    return; // after conversion
                if (cell.image.isNull())
                    continue; // failed conversion must not clear the last valid preview
                r.cells.push_back(std::move(cell));
            }

            if (ctx.isCancelled())
                return;

            // Marshal to the UI thread through qApp (outlives this workspace).
            // The queued lambda re-checks the guard AND the generation/pane-count
            // match before touching any widget.
            QMetaObject::invokeMethod(
                qApp,
                [guard, r]()
                {
                    CompareWorkspace *ws = guard.data();
                    if (!ws)
                        return;
                    ws->applyDisplayBatchResult(r);
                },
                Qt::QueuedConnection);
        });
}

void CompareWorkspace::scheduleDisplayMaterialization(const std::vector<int> &dirtyPanes)
{
    // Latest-wins: cancel any in-flight batch and start a fresh generation.
    // Cancellation alone is not enough — a task may already be past its final
    // check when a newer request arrives, so the delivery is also guarded by
    // the generation and pane count on the UI thread.
    if (m_displayTask)
        TaskScheduler::cancel(m_displayTask);
    m_displayTask.reset();
    ++m_displayGen;

    // Normalize/deduplicate the requested indices and include every current
    // pane whose image is still null.
    const int paneCount = static_cast<int>(m_cellViews.size());
    std::vector<int> panes;
    panes.reserve(static_cast<size_t>(paneCount));
    auto add = [&panes, paneCount](int idx)
    {
        if (idx < 0 || idx >= paneCount)
            return;
        if (std::find(panes.cbegin(), panes.cend(), idx) != panes.cend())
            return;
        panes.push_back(idx);
    };
    for (int i = 0; i < paneCount; ++i)
        if (m_cellViews[i] && m_cellViews[i]->image().isNull())
            add(i);
    for (int idx : dirtyPanes)
        add(idx);
    if (panes.empty())
        return; // nothing to materialize; the stale task is already cancelled

    // Snapshot everything the worker needs BY VALUE. The worker only touches
    // these captures — no `this`, no QObject/QWidget. ImageData copies share
    // their pixel buffers, so the worker holds the pixels alive cheaply.
    std::vector<ImageData> pixels;
    std::vector<mviewer::domain::ImageMetadata> metadata;
    std::vector<DisplayRequest> displayRequests;
    pixels.reserve(static_cast<size_t>(paneCount));
    metadata.reserve(static_cast<size_t>(paneCount));
    displayRequests.reserve(static_cast<size_t>(paneCount));
    for (int i = 0; i < paneCount; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        pixels.push_back(img ? img->pixels() : ImageData());
        metadata.push_back(img ? img->metadata() : mviewer::domain::ImageMetadata{});
        if (img && !img->pixels().isNull())
        {
            displayRequests.push_back(
                {displayLodTarget(i, img->pixels()),
                 QRect(QPoint(0, 0), QSize(img->pixels().width, img->pixels().height)), false});
        }
        else if (i < static_cast<int>(m_comparePaths.size()) &&
                 !m_comparePaths[static_cast<size_t>(i)].empty())
        {
            displayRequests.push_back(sourceDisplayRequest(i));
        }
        else
        {
            displayRequests.push_back({});
        }
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    const uint64_t gen = m_displayGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = startDisplayMaterialization(pixels, metadata, displayRequests, adjusts, panes,
                                              paneCount, gen, m_comparePaths, guard);
    if (!handle)
    {
        // submit() refused the task (pool paused / back-pressured). Keep the
        // last delivered pane images — never fall back to synchronous
        // conversion on the UI thread. The generation already advanced, so a
        // later schedule supersedes this state.
        return;
    }
    m_displayTask = handle;
}

void CompareWorkspace::applyDisplayBatchResult(const DisplayBatchResult &r)
{
    if (r.generation != m_displayGen)
        return; // superseded by a newer batch
    if (r.paneCount != static_cast<int>(m_cellViews.size()))
        return; // the pane layout changed while the batch was in flight

    // This is the current generation's terminal delivery: release the handle.
    m_displayTask.reset();

    for (const auto &cell : r.cells)
    {
        if (cell.index < 0 || cell.index >= static_cast<int>(m_cellViews.size()))
            continue;
        RawImageView *view = m_cellViews[static_cast<size_t>(cell.index)];
        if (!view)
            continue;
        const QSize oldSize = view->image().size();
        const QSize oldSourceSize = view->sourceSize();
        const double oldScale = view->scale();
        const QPointF oldOffset = view->offset();
        view->setImage(cell.image, cell.sourceSize, cell.sourceRect);
        // M15/M48: the transform lives in full source coordinates. Preserve it
        // across both ordinary LOD replacement and covered-region updates; a
        // region's pixel dimensions are expected to change as the viewport
        // moves, and must never reset the user's zoom/pan.
        if (!oldSize.isEmpty() && view->sourceSize() == oldSourceSize)
            view->setTransform(oldScale, oldOffset);
        if (cell.sourceRect != QRect(QPoint(0, 0), cell.sourceSize))
            view->clearOverlay();
    }

    // Refresh the Pixel Inspector once after the newest display lands when its
    // panel and a sample position are active. Committed metrics stay deferred:
    // they are driven by the separate diff batch on slider release.
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0 && m_lastInspectY >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);

    updateTemporaryCompareAvailability();
    update();
}



