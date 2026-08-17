#include "compareworkspace_p.h"

#include "core/image/ImageFrame.h"
#include "core/image/SourceImage.h"

#include <utility>

// M47: Compare analysis-support full frames are only loaded when the source's
// RGB materialization fits comfortably under Qt's 256 MB allocation limit
// (60 MP * 3 B = 180 MB, leaving headroom for QImage copies). Infeasible
// sources display through the source-backed LOD path only.
constexpr qint64 kCompareAnalysisFeasiblePixels = 60 * 1000 * 1000; // 60 MP

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent)
{
    m_lifetime = mviewer::core::AsyncLifetimeToken::create();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    buildSyncControls();
    QHBoxLayout *modeLayout = nullptr;
    QHBoxLayout *viewLayout = nullptr;
    QHBoxLayout *toolLayout = nullptr;
    auto *toolbarContainer = buildToolbarContainer(modeLayout, viewLayout, toolLayout);
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
    buildToolbarActions(toolLayout);
    auto *leftLay = new QVBoxLayout;
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);
    leftLay->addWidget(toolbarContainer);
    leftLay->addLayout(pages, 1);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addLayout(leftLay, 1);
    root->addWidget(m_sidePanel);
}

CompareWorkspace::~CompareWorkspace()
{
    // M46: invalidate the consumer-lifetime token first so the repository
    // suppresses every not-yet-started client delivery for this workspace.
    // The batch cancellation below then also waits for any delivery that
    // already started, closing the decode-done vs destruction race.
    m_lifetime->invalidate();
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
}

void CompareWorkspace::setImages(const QStringList &paths)
{
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
    // A newer load supersedes a session that was pending on the older one.
    m_pendingSession.reset();

    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString &p : paths)
        stdPaths.push_back(p.toStdString());
    const int requested = static_cast<int>(paths.size());
    if (requested == 0)
    {
        finishLoad({}, 0);
        return;
    }

    auto batch = std::make_shared<LoadBatch>();
    batch->generation = gen;
    batch->frames = std::make_shared<std::vector<std::shared_ptr<ImageFrame>>>(requested);
    batch->remaining = std::make_shared<std::atomic<int>>(requested);
    batch->failed = std::make_shared<std::atomic<int>>(0);
    batch->requests.reserve(static_cast<size_t>(requested));
    for (int i = 0; i < requested; ++i)
        batch->requests.push_back(std::make_unique<LoadRequest>());
    m_loadBatch = batch;
    queueLoadRequests(batch, stdPaths);
}

void CompareWorkspace::queueLoadRequests(const std::shared_ptr<LoadBatch> &batch,
                                         const std::vector<std::string> &paths)
{
    auto self = std::make_shared<QPointer<CompareWorkspace>>(this);
    auto lifetime = m_lifetime;
    const ImageLoadOptions opts{true, false, 256};
    // M47: the pane display no longer depends on the full frame. Sources whose
    // full-resolution materialization is infeasible (RGB > Qt's 256 MB
    // allocation limit) skip the full load entirely — their panes display
    // through the source-backed LOD path, and they must not count as load
    // failures. The probe is a cheap header read (never a decode).
    m_infeasibleCount = 0;
    m_comparePaths = paths;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        auto &request = *batch->requests[i];
        bool infeasible = false;
        std::shared_ptr<mviewer::core::SourceImage> source;
        {
            source = mviewer::core::SourceImage::open(paths[i]);
            if (source)
            {
                const qint64 px =
                    static_cast<qint64>(source->metadata().width) * source->metadata().height;
                infeasible = px > kCompareAnalysisFeasiblePixels;
            }
        }
        if (infeasible && source)
        {
            ++m_infeasibleCount;
            // Keep the pane: a metadata-only placeholder frame preserves the
            // requested pane index in the engine (so the LOD display worker's
            // pane/path mapping stays aligned). The pane displays through the
            // source-backed LOD path; this is a skip, not a failure — it must
            // not count as a load failure and must not shrink the grid.
            (*batch->frames)[i] =
                std::make_shared<ImageFrame>(source->metadata(), ImageData());
            if (accountLoadRequest(batch, i, nullptr, false))
            {
                // The last request was accounted synchronously (an all-
                // infeasible set has no async callbacks to deliver the
                // terminal load) — queue it like the async path does.
                QMetaObject::invokeMethod(
                    qApp,
                    [self, batch]()
                    {
                        CompareWorkspace *ws = self->data();
                        if (!ws || batch->generation != ws->m_loadGen)
                            return;
                        ws->finishLoad(*batch->frames,
                                       batch->failed->load(std::memory_order_relaxed));
                    },
                    Qt::QueuedConnection);
            }
            continue;
        }
        auto handle = mviewer::application::ImageLoadingService::instance().loadAsyncCancellable(
            paths[i],
            [self, batch, i](const mviewer::application::ImageLoadingService::Result &res)
            {
                if (!CompareWorkspace::accountLoadRequest(batch, i, &res) || !qApp)
                    return;
                QMetaObject::invokeMethod(
                    qApp,
                    [self, batch]()
                    {
                        CompareWorkspace *ws = self->data();
                        if (!ws || batch->generation != ws->m_loadGen)
                            return;
                        ws->finishLoad(*batch->frames,
                                       batch->failed->load(std::memory_order_relaxed));
                    },
                    Qt::QueuedConnection);
            },
            opts, lifetime);
        std::lock_guard<std::mutex> lk(batch->handlesMutex);
        request.handle = std::move(handle);
    }
}

bool CompareWorkspace::accountLoadRequest(
    const std::shared_ptr<LoadBatch> &batch, size_t index,
    const mviewer::application::ImageLoadingService::Result *result, bool countAsFailure)
{
    if (!batch || index >= batch->requests.size())
        return false;
    auto &request = *batch->requests[index];
    if (request.accounted.exchange(true, std::memory_order_acq_rel))
        return false;

    if (result && result->success() && result->frame)
        (*batch->frames)[index] = result->frame;
    else if (countAsFailure)
        batch->failed->fetch_add(1, std::memory_order_relaxed);

    return batch->remaining->fetch_sub(1, std::memory_order_acq_rel) == 1;
}

void CompareWorkspace::cancelLoadBatch(const std::shared_ptr<LoadBatch> &batch)
{
    if (!batch)
        return;
    for (size_t i = 0; i < batch->requests.size(); ++i)
    {
        mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
        {
            std::lock_guard<std::mutex> lk(batch->handlesMutex);
            handle = std::move(batch->requests[i]->handle);
        }
        mviewer::application::ImageLoadingService::instance().cancelAsync(handle);
        // ImageRepository deliberately suppresses callbacks after cancel. Do
        // the batch's exactly-once accounting locally so cancellation cannot
        // strand the terminal completion on a queued request.
        accountLoadRequest(batch, i, nullptr);
    }
}

void CompareWorkspace::finishLoad(const std::vector<std::shared_ptr<ImageFrame>> &frames,
                                  int failedCount)
{
    m_loadInFlight = false;
    m_loadBatch.reset();
    m_engine.setFrames(frames);

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
    rebuildCells();
    schedulePostLayoutFit();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();

    // P0-4: split / swipe only make sense for exactly two images.
    const bool two = m_engine.imageCount() == 2;
    if (m_splitChk)
    {
        if (!two)
            m_splitChk->setChecked(false);
        m_splitChk->setEnabled(two);
    }
    if (m_swipeChk)
    {
        if (!two)
            m_swipeChk->setChecked(false);
        m_swipeChk->setEnabled(two);
    }
    // A-4.1: overlay mode also only for two images.
    if (m_overlayChk)
    {
        if (!two)
            m_overlayChk->setChecked(false);
        m_overlayChk->setEnabled(two);
    }
    // M23: checkerboard mode also only for two images.
    if (m_checkerChk)
    {
        if (!two)
            m_checkerChk->setChecked(false);
        m_checkerChk->setEnabled(two);
    }
    if (m_grid && !two)
        m_grid->setVisible(true);
    updateCanvasModeVisibility();
    setFocus();
    // P0-2: publish the compare set + reference to the app-wide SelectionModel so
    // Metadata/Analysis/Export stay in sync with what is being compared.
    if (m_selection)
    {
        m_selection->setCompared(comparedImages());
        m_selection->setFocused(focusImagePath());
    }

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
    if (m_gridRowsSpin)
        m_gridRowsSpin->setEnabled(custom);
    if (m_gridColsSpin)
        m_gridColsSpin->setEnabled(custom);

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
    rebuildCells();
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
    rebuildCells();
    schedulePostLayoutFit();
    refreshLinkMarkers();
    update();
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

    // P0: Write the reference cell's image back to the global SelectionModel so
    // the rest of the app (MetadataPanel/AnalysisPanel/Export/etc.) stays in sync.
    if (locking && m_selection && newFocus >= 0)
    {
        const int poolIdx = m_pairIndex + newFocus;
        if (poolIdx >= 0 && poolIdx < m_imagePool.size())
            m_selection->setCurrentImage(m_imagePool[poolIdx]);
    }
    // P0-2: publish the locked reference to the app-wide SelectionModel.
    if (m_selection)
        m_selection->setFocused(locking ? focusImagePath() : QString());

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
