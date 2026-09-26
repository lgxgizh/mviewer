#include "compareworkspace_p.h"

#include "core/image/ImageFrame.h"
#include "core/image/SourceImage.h"

#include <QScreen>

#include <algorithm>
#include <utility>

// M47: Compare analysis-support full frames are only loaded when the source's
// RGB materialization fits comfortably under Qt's 256 MB allocation limit
// (60 MP * 3 B = 180 MB, leaving headroom for QImage copies). Infeasible
// sources display through the source-backed LOD path only.

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent)
{
    m_lifetime = mviewer::core::AsyncLifetimeToken::create();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    buildSyncControls();
    QHBoxLayout *modeLayout = nullptr;
    QHBoxLayout *viewLayout = nullptr;
    QHBoxLayout *toolLayout = nullptr;
    QHBoxLayout *toolActionsLayout = nullptr;
    auto *toolbarContainer =
        buildToolbarContainer(modeLayout, viewLayout, toolLayout, toolActionsLayout);
    buildModeControls(modeLayout, viewLayout);
    buildDiffControls(toolLayout);
    buildViewControls(viewLayout);
    m_grid = new QWidget;
    m_layout = new QGridLayout(m_grid);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);

    // M34: the grid scrolls inside "compareGridPage"; a stacked layout swaps it
    // with the dedicated "compareCanvas" widget in split/swipe/overlay/checker
    // modes. Construction lives in the render TU (ADR 014).
    auto *pages = buildCanvasPage();

    // P0 #③: right-side inspector/histogram panel (collapsible).
    m_sidePanel = new QWidget(this);
    m_sidePanel->setFixedWidth(280);
    auto *sideLay = new QVBoxLayout(m_sidePanel);
    sideLay->setContentsMargins(4, 4, 4, 4);
    sideLay->setSpacing(6);

    // M23: inspector (multi color-space) + histogram (channels/log/ROI) +
    // metrics — built in compareworkspace_analysis.cpp (ADR 014 TU split).
    buildAnalysisPanel(sideLay);

    // M16.2: per-cell image edit panel (collapsible section inside side panel)
    buildEditPanel(sideLay);

    m_sidePanel->setVisible(false);
    buildToolbarActions(toolActionsLayout);
    auto *leftLay = new QVBoxLayout;
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);
    leftLay->addWidget(toolbarContainer);
    leftLay->addWidget(buildStatusStrip());
    leftLay->addLayout(pages, 1);

    auto *root = new QHBoxLayout(this);
    root->setSpacing(0);
    root->addLayout(leftLay, 1);
    root->addWidget(m_sidePanel);
    applyCompareSafeInsets();
    updateActionAvailability();
    updateROIAvailabilityStatus();
    syncContextualCompareControls();
}

void CompareWorkspace::applyCompareSafeInsets()
{
    QLayout *root = layout();
    if (!root)
        return;
    int left = 8;
    int top = 6;
    int right = 8;
    int bottom = 8;
    const Qt::WindowStates state = window() ? window()->windowState() : windowState();
    const bool coverScreen =
        state.testFlag(Qt::WindowFullScreen) || state.testFlag(Qt::WindowMaximized);
    if (coverScreen)
    {
        if (QScreen *screen = this->screen())
        {
            const QRect widgetScreen(mapToGlobal(QPoint(0, 0)), size());
            if (widgetScreen.isValid() && widgetScreen.width() > 0 && widgetScreen.height() > 0)
            {
                const QRect overlap = widgetScreen.intersected(screen->availableGeometry());
                if (overlap.isValid() && !overlap.isEmpty())
                {
                    left = std::max(8, overlap.left() - widgetScreen.left());
                    top = std::max(6, overlap.top() - widgetScreen.top());
                    right = std::max(8, widgetScreen.right() - overlap.right());
                    bottom = std::max(8, widgetScreen.bottom() - overlap.bottom());
                }
            }
        }
    }
    const QMargins next(left, top, right, bottom);
    if (root->contentsMargins() != next)
        root->setContentsMargins(next);
}

CompareWorkspace::~CompareWorkspace()
{
    // M46: invalidate the consumer-lifetime token first so the repository
    // suppresses every not-yet-started client delivery for this workspace.
    // The batch cancellation below then also waits for any delivery that
    // already started, closing the decode-done vs destruction race.
    m_lifetime->invalidate();
    cancelPairPrefetch();
    // A batch completion is bookkeeping, not cancellation. Every request is
    // accounted exactly once when a batch is superseded so a cancelled queued
    // decode cannot leave `remaining` permanently non-zero. The generation
    // bump below remains the final stale-delivery guard for callbacks that were
    // already queued on qApp.
    ++m_loadGen;
    if (m_loadBatch)
    {
        cancelLoadBatch(m_loadBatch);
        m_loadBatch.reset();
    }
    m_loadInFlight = false;

    // Invalidate the in-flight async diff batch. The worker marshals its
    // result via qApp and re-checks the generation, which we bump here, so a
    // completion that is already queued cannot paint into a destroyed widget
    // (the QPointer guard covers the destroyed-owner case).
    if (m_diffTask)
        TaskScheduler::cancel(m_diffTask);
    m_diffTask.reset();
    ++m_diffGen;

    // Same lifetime handling for the async pane-materialization batch.
    if (m_displayTask)
        TaskScheduler::cancel(m_displayTask);
    m_displayTask.reset();
    ++m_displayGen;

    // Same lifetime handling for the async pane-histogram batch.
    if (m_histTask)
        TaskScheduler::cancel(m_histTask);
    m_histTask.reset();
    ++m_histGen;

    // M60: invalidate pending source ROI measurements before widget teardown.
    if (m_roiTask)
        TaskScheduler::cancel(m_roiTask);
    m_roiTask.reset();
    ++m_roiGen;
}

void CompareWorkspace::setDisplayColorContext(const mviewer::core::DisplayColorContext &target)
{
    if (target.cacheKey() == m_displayColorTarget.cacheKey())
        return;
    m_displayColorTarget = target;
    std::vector<int> all;
    all.reserve(m_cellViews.size());
    for (int i = 0; i < static_cast<int>(m_cellViews.size()); ++i)
        all.push_back(i);
    scheduleDisplayMaterialization(all);
    update();
}

void CompareWorkspace::setSelectionModel(SelectionModel *sel)
{
    m_selection = sel;
}

void CompareWorkspace::exclusiveMode(QCheckBox *keepOn)
{
    // Split / Swipe / Overlay / Checkerboard are mutually exclusive (2 images).
    auto uncheck = [keepOn](QCheckBox *c)
    {
        if (c && c != keepOn && c->isChecked())
            c->setChecked(false);
    };
    uncheck(m_splitChk);
    uncheck(m_swipeChk);
    uncheck(m_overlayChk);
    uncheck(m_checkerChk);
    // Blink and the dedicated canvas modes render through different surfaces.
    // Keeping both checked would leave the canvas visible while Blink only
    // toggles the hidden grid panes, so they must be mutually exclusive too.
    uncheck(m_blinkChk);
}

void CompareWorkspace::setImages(const QStringList &paths)
{
    setImages(paths, {});
}

void CompareWorkspace::setImages(const QStringList &paths, const QVector<int> &frameIndices)
{
    endTemporaryCompare();
    // Neighbor preloads are consumed via promote in queueLoadRequests; leftovers
    // are cancelled at the end of that queue so next/prev can hit a warm decode.
    // A new compare set supersedes any in-flight ROI calculation immediately;
    // preserving only the geometry for a possible same-dimension navigation
    // restore prevents stale source statistics from crossing image pairs.
    if (m_roiTask)
        TaskScheduler::cancel(m_roiTask);
    m_roiTask.reset();
    ++m_roiGen;
    clearROIStatsDisplay();
    setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle);
    if (!m_roiLinked)
        m_lastSelection = {};
    // M28 P1-01: Compare loads are ASYNC. Decoding happens on the DecodePool,
    // never on the UI thread: setImages() returns immediately, and the frames
    // are applied by finishLoad() on the UI thread when every request in this
    // batch completes. A newer setImages() supersedes an in-flight batch via
    // the generation counter, so stale completions can never overwrite the
    // current compare set (A -> B -> A is safe).
    const uint64_t gen = ++m_loadGen;
    if (m_loadBatch)
    {
        cancelLoadBatch(m_loadBatch);
        m_loadBatch.reset();
    }
    m_loadInFlight = true;
    updateActionAvailability();
    // A newer load supersedes a session that was pending on the older one.
    m_pendingSession.reset();

    std::vector<std::string> stdPaths;
    std::vector<int> stdFrameIndices;
    stdPaths.reserve(paths.size());
    stdFrameIndices.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i)
    {
        const QString &p = paths.at(i);
        stdPaths.push_back(p.toUtf8().toStdString());
        stdFrameIndices.push_back(i < frameIndices.size() ? std::max(0, frameIndices.at(i)) : 0);
    }
    const int requested = static_cast<int>(paths.size());
    if (requested > 0)
    {
        // Publish loading state before queueing any probe. Soft keep-grid:
        // when panes already show a prior pair, keep them visible (stale-
        // while-revalidate) instead of blanking to the full-page spinner.
        if (m_compareLoadingLabel)
            m_compareLoadingLabel->setText(tr("正在加载 %1 张图片…").arg(requested));
        const bool softKeepGrid = m_engine.imageCount() > 0;
        m_softPairReload = softKeepGrid;
        if (softKeepGrid)
        {
            showCompareStatus(tr("正在加载下一组…"), 2500);
            applySoftReloadPlaceholders(stdPaths);
        }
        else
        {
            if (m_compareLoadingProgress)
            {
                m_compareLoadingProgress->setRange(0, 0);
                m_compareLoadingProgress->setVisible(true);
            }
            if (m_pageStack && m_compareLoadingPage)
                m_pageStack->setCurrentWidget(m_compareLoadingPage);
        }
        update();
    }
    if (requested == 0)
    {
        cancelPairPrefetch();
        m_softPairReload = false;
        finishLoad({}, 0);
        return;
    }

    auto batch = std::make_shared<LoadBatch>();
    batch->generation = gen;
    batch->frames = std::make_shared<std::vector<std::shared_ptr<ImageFrame>>>(requested);
    batch->remaining = std::make_shared<std::atomic<int>>(requested);
    batch->failed = std::make_shared<std::atomic<int>>(0);
    batch->infeasible = std::make_shared<std::atomic<int>>(0);
    batch->requests.reserve(static_cast<size_t>(requested));
    for (int i = 0; i < requested; ++i)
        batch->requests.push_back(std::make_unique<LoadRequest>());
    m_loadBatch = batch;
    queueLoadRequests(batch, stdPaths, stdFrameIndices);
}

void CompareWorkspace::finishLoad(const std::vector<std::shared_ptr<ImageFrame>> &frames,
                                  int failedCount, int infeasibleCount)
{
    m_loadInFlight = false;
    m_loadBatch.reset();
    m_infeasibleCount = infeasibleCount;
    m_engine.setFrames(frames);
    if (m_compareLoadingProgress)
        m_compareLoadingProgress->setVisible(false);
    if (m_engine.imageCount() == 0 && m_compareLoadingLabel)
    {
        m_compareLoadingLabel->setText(
            failedCount > 0 ? tr("没有可用的图片。\n请检查文件是否存在、完整且受支持。")
                            : tr("没有可用的图片。\n请返回浏览器选择图片。"));
    }

    // M24 (B#7): failed loads are dropped by the engine — tell the user why
    // the grid has fewer cells than requested instead of silently shrinking.
    // M47: sources skipped as analysis-infeasible display through the source-
    // backed LOD path (their panes are NOT empty; the engine holds a
    // metadata-only placeholder for them) and are excluded from the failure
    // accounting above, so `failedCount` here is exactly the real failure
    // count and the placeholder panes never trigger a warning.
    const int requested = static_cast<int>(frames.size());
    const int loaded = m_engine.imageCount() - m_infeasibleCount;
    if (failedCount > 0)
    {
        emit loadWarning(
            tr("%1 张图片无法加载（文件损坏、缺失或不支持），已保留 %2 张可用的进行对比。")
                .arg(failedCount)
                .arg(loaded));
    }
    // A-4: loading a fresh comparison set should not inherit adjustments from
    // the previous session; applySession will repopulate persisted values.
    m_cellAdjusts.clear();
    m_softPairReload = false;
    if (!finishLoadInPlaceIfPossible(frames))
    {
        clearSoftLoadingIndicators();
        rebuildCells();
    }
    syncEditCellAfterLoad();
    schedulePostLayoutFit();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();

    // P0-4 / A-4.1 / M23: split, swipe, overlay and checkerboard only make sense
    // for exactly two images, and so does blink (see the TU for the blink case).
    disarmSingleImageModes();
    updateActionAvailability();
    if (m_grid && m_engine.imageCount() != 2)
        m_grid->setVisible(true);
    updateCanvasModeVisibility();
    updateTemporaryCompareAvailability();
    updateLayoutStatus();
    updateROIAvailabilityStatus();
    prefetchNeighborPairs();
    setFocus();
    // P0-2: publish the compare set + reference to the app-wide SelectionModel so
    // Metadata/Analysis/Export stay in sync with what is being compared.
    if (m_selection)
    {
        m_selection->setCompared(comparedImages());
        m_selection->setFocused(focusImagePath());
    }
    updateFrameControl();

    // M28 P1-01: a session that was applied while the load was in flight is
    // replayed once the frames exist (openCompare -> setImages -> applySession).
    if (m_pendingSession)
    {
        const auto session = *m_pendingSession;
        m_pendingSession.reset();
        QPointer<CompareWorkspace> guard(this);
        // schedulePostLayoutFit() was queued first. Replay persisted transforms
        // after that settled-geometry Fit so it cannot reset the saved ratio.
        QTimer::singleShot(0, this,
                           [guard, session]()
                           {
                               if (guard)
                                   guard->applySession(session);
                           });
    }
}

QStringList CompareWorkspace::comparedImages() const
{
    QStringList out;
    const int n = m_engine.imageCount();
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        if (img)
            out.push_back(QString::fromStdString(img->metadata().filePath));
    }
    return out;
}

bool CompareWorkspace::isSyncEnabled() const
{
    return m_syncZoom || m_syncDrag;
}

void CompareWorkspace::setSyncEnabled(bool on)
{
    m_syncZoom = on;
    m_syncDrag = on;
    m_syncZoomChk->setChecked(on);
    m_syncDragChk->setChecked(on);
    m_engine.setSyncMode(on, on);
}

// rebuildCells() lives in compareworkspace_render.cpp (ADR 014 TU split).

void CompareWorkspace::onLayoutChanged()
{
    if (!m_layoutCombo)
        return;
    const int idx = m_layoutCombo->currentIndex();
    const bool custom = (idx == 6); // 自定义 M×N
    if (m_gridColsSpin)
        m_gridColsSpin->setEnabled(custom);
    syncContextualCompareControls();

    int cols = 0;
    switch (idx)
    {
    case 1:
        cols = 1;
        break; // 单列
    case 2:
        cols = 2;
        break; // 2 列
    case 3:
        cols = 3;
        break; // 3 列
    case 4:
        cols = 4;
        break; // 4 列
    case 5:
        cols = m_engine.imageCount();
        break; // 一行
    case 6:
        // A-4.2: custom grid — columns from spin; rows derived by engine.
        cols = m_gridColsSpin ? m_gridColsSpin->value() : 2;
        break;
    default:
        cols = 0;
        break; // 自动
    }
    m_engine.setColumns(cols);
    if (!m_cellViews.isEmpty() && m_cellViews.size() == m_engine.imageCount())
        relayoutGridKeepingPanes();
    else
        rebuildCells();
    updateLayoutStatus();
    schedulePostLayoutFit();
    refreshLinkMarkers();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
}

void CompareWorkspace::onCustomGridChanged()
{
    if (!m_layoutCombo || m_layoutCombo->currentIndex() != 6)
        return;
    // Re-apply custom columns; rows are informational (engine packs by cols).
    const int cols = m_gridColsSpin ? m_gridColsSpin->value() : 2;
    m_engine.setColumns(cols);
    if (!m_cellViews.isEmpty() && m_cellViews.size() == m_engine.imageCount())
        relayoutGridKeepingPanes();
    else
        rebuildCells();
    updateLayoutStatus();
    schedulePostLayoutFit();
    refreshLinkMarkers();
    update();
}

void CompareWorkspace::updateFrameControl()
{
    if (!m_frameLabel || !m_frameSpin)
        return;
    int index = m_editIdx;
    if (index < 0)
        index = m_hoverIdx;
    if (index < 0)
        index = 0;
    const ImageFrame *frame = m_engine.imageAt(index);
    const bool multi = frame && frame->sequenceInfo().frameCount > 1;
    const int count = multi ? frame->sequenceInfo().frameCount : 1;
    const QSignalBlocker blocker(m_frameSpin);
    m_frameLabel->setVisible(multi);
    m_frameSpin->setVisible(multi);
    m_frameSpin->setEnabled(multi);
    m_frameSpin->setRange(1, count);
    if (multi)
        m_frameSpin->setValue(std::clamp(frame->frameIndex() + 1, 1, count));
}

void CompareWorkspace::onFrameControlChanged(int oneBasedIndex)
{
    int index = m_editIdx;
    if (index < 0)
        index = m_hoverIdx;
    if (index < 0 || index >= m_engine.imageCount())
        return;
    const ImageFrame *current = m_engine.imageAt(index);
    if (!current || current->sequenceInfo().frameCount <= 1)
        return;
    const int target = std::clamp(oneBasedIndex - 1, 0, current->sequenceInfo().frameCount - 1);
    if (target == current->frameIndex())
        return;

    const QStringList paths = comparedImages();
    if (index >= paths.size())
        return;
    auto session = compareSession();
    if (session.frameIndices.size() < static_cast<size_t>(paths.size()))
        session.frameIndices.resize(static_cast<size_t>(paths.size()), 0);
    session.frameIndices[static_cast<size_t>(index)] = target;

    QVector<int> frameIndices;
    frameIndices.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i)
        frameIndices.append(i < static_cast<int>(session.frameIndices.size())
                                ? std::max(0, session.frameIndices[static_cast<size_t>(i)])
                                : 0);
    setImages(paths, frameIndices);
    applySession(session);
}

void CompareWorkspace::updateLayoutStatus()
{
    if (!m_layoutStatusLabel)
        return;
    const auto layout = m_engine.layout();
    m_layoutStatusLabel->setText(tr("网格: %1 行 × %2 列").arg(layout.rows).arg(layout.cols));
}

void CompareWorkspace::onSideToggled(bool on)
{
    if (!m_sidePanel)
        return;
    m_sidePanel->setVisible(on);
    if (on)
    {
        refreshHistograms();
        refreshAllDiffOverlays();
    }
    updateROISurfaces();
    update();
}

void CompareWorkspace::onCrosshairMoved(RawImageView *view, const QPointF &pos)
{
    if (!view)
        return;
    // Track the hovered cell so the focus-lock button knows which cell to pin.
    m_hoverIdx = view->cellIndex();

    if (!m_crosshairChk || !m_crosshairChk->isChecked())
        return;

    const bool valid = pos.x() >= 0.0 && pos.y() >= 0.0;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        if (valid)
            m_cellViews[i]->setCrosshair(pos);
        else
            m_cellViews[i]->clearCrosshair();
    }
    // Sample every cell at the synced image-space point. M30: route through the
    // coalescer so this crosshair path cannot double-render the inspector that
    // the same hover's pixelInfo signal already requested.
    if (valid && m_sidePanel && m_sidePanel->isVisible())
        requestInspectorUpdate(qRound(pos.x()), qRound(pos.y()));
}

void CompareWorkspace::onFocusRequested(int cellIndex)
{
    // Toggle the locked reference: re-clicking the focused cell clears it.
    const int newFocus = (cellIndex == m_focusIndex) ? -1 : cellIndex;
    const bool locking = newFocus >= 0;

    m_focusIndex = newFocus;
    if (m_focusBtn)
    {
        // Sync the button without emitting toggled: the button's toggled handler
        // re-enters onFocusRequested and would immediately clear/replace the
        // requested focus (visible when a pane double-click drives this path).
        const QSignalBlocker blocker(m_focusBtn);
        m_focusBtn->setChecked(locking);
    }
    if (m_focusLabel)
        m_focusLabel->setText(locking ? tr("基准: %1").arg(newFocus + 1) : tr("基准: —"));

    // Resolve the actual compared pane before updating the global SelectionModel.
    // The navigation pool may contain images not present in a non-contiguous pair.
    const QString panePath = locking ? focusImagePath() : QString();
    if (m_selection)
    {
        if (locking && !panePath.isEmpty())
            m_selection->setCurrentImage(panePath);
        m_selection->setFocused(panePath);
    }

    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        m_cellViews[i]->setFocused(i == m_focusIndex);
    }

    // Recompute all diff overlays against the new base (async batch, latest-wins).
    refreshAllDiffOverlays();
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);
    update();
}

// ─── M16.2: Per-cell image adjustments ───────────────────────────────────────
