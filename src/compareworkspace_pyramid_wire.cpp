#include "compareworkspace_caption.h"
#include "compareworkspace_display_state.h"
#include "compareworkspace_p.h"

#include "core/render/DisplayResample.h"

#include <QTimer>
#include <QtGlobal>
#include <algorithm>

namespace
{
void rememberPyramidDelivery(mviewer::ui::CompareSessionRuntime *session, int pane,
                             const std::string &path, uint64_t contentKey, const QImage &image,
                             const QSize &sourceSize, const mviewer::ui::CompareDisplayRequest &req,
                             bool full)
{
    if (!session || pane < 0)
        return;
    if (session->panePyramids.size() <= static_cast<size_t>(pane))
        session->panePyramids.resize(static_cast<size_t>(pane) + 1);
    auto &slot = session->panePyramids[static_cast<size_t>(pane)];
    if (slot.path != path || slot.contentKey != contentKey)
    {
        slot = {};
        slot.path = path;
        slot.contentKey = contentKey;
    }
    slot.sourceSize = sourceSize;
    const int edge = std::max(req.target.width(), req.target.height());
    for (size_t i = 0; i < slot.levels.size(); ++i)
    {
        const int have = std::max(slot.levels[i].target.width(), slot.levels[i].target.height());
        if (have == edge && slot.levels[i].region == req.region)
        {
            slot.levels[i] = req;
            slot.images[i] = image;
            if (full)
                slot.haveFull = true;
            return;
        }
    }
    constexpr size_t kMaxLevels = 3;
    if (slot.levels.size() >= kMaxLevels)
    {
        slot.levels.erase(slot.levels.begin());
        slot.images.erase(slot.images.begin());
    }
    slot.levels.push_back(req);
    slot.images.push_back(image);
    if (full)
        slot.haveFull = true;
}

mviewer::core::DisplayRasterLevel rasterLevel(int targetW, int targetH, const QRect &covered)
{
    mviewer::core::DisplayRasterLevel level;
    if (targetW <= 0 || targetH <= 0 || !covered.isValid())
        return level;
    level.targetWidth = targetW;
    level.targetHeight = targetH;
    level.coveredSourceWidth = covered.width();
    level.coveredSourceHeight = covered.height();
    level.originX = covered.x();
    level.originY = covered.y();
    return level;
}

bool skipLateProvisional(bool provisional, bool preview, int index, const QImage &shownImage,
                         const QRect &shownRect, const QImage &incomingImage,
                         const QRect &incomingRect,
                         const mviewer::ui::CompareSessionRuntime *session)
{
    if (!provisional)
        return false;
    const bool blurrier = mviewer::core::isBlurrierDisplayStandIn(
        rasterLevel(shownImage.width(), shownImage.height(), shownRect),
        rasterLevel(incomingImage.width(), incomingImage.height(), incomingRect));
    if (blurrier)
        return true;
    // A fast preview may replace a sharp raster when it is not blurrier
    // (zoom-in or pan). The initial low-edge provisional still must not
    // paint over a full delivery.
    if (preview)
        return false;
    return session && index >= 0 && index < static_cast<int>(session->panePyramids.size()) &&
           session->panePyramids[static_cast<size_t>(index)].haveFull;
}

// Records whether pane `index` now shows a preview. Returns true when this
// delivery only upgrades a preview to the sharp raster.
bool swapPreviewState(mviewer::ui::CompareSessionRuntime *session, int index, bool preview)
{
    if (!session || index < 0)
        return false;
    auto &flags = session->displayIsPreview;
    if (flags.size() <= static_cast<size_t>(index))
        flags.resize(static_cast<size_t>(index) + 1, 0);
    const bool upgrade = flags[static_cast<size_t>(index)] != 0 && !preview;
    flags[static_cast<size_t>(index)] = preview ? 1 : 0;
    return upgrade;
}

bool tryPaintFromPyramid(mviewer::ui::CompareSessionRuntime *session,
                         const QList<RawImageView *> &cellViews, int pane, uint64_t contentKey,
                         const mviewer::ui::CompareDisplayRequest &desired)
{
    if (!session || pane < 0 || pane >= static_cast<int>(session->panePyramids.size()))
        return false;
    auto &slot = session->panePyramids[static_cast<size_t>(pane)];
    if (slot.contentKey != contentKey || slot.images.empty() || pane >= cellViews.size() ||
        !cellViews[pane])
        return false;

    const int count = static_cast<int>((std::min)(slot.levels.size(), slot.images.size()));
    std::vector<mviewer::core::DisplayRasterLevel> levels;
    levels.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const QImage &image = slot.images[static_cast<size_t>(i)];
        const auto &lvl = slot.levels[static_cast<size_t>(i)];
        levels.push_back(image.isNull()
                             ? mviewer::core::DisplayRasterLevel{}
                             : rasterLevel(image.width(), image.height(), lvl.sourceRect));
    }
    RawImageView *view = cellViews[pane];
    const mviewer::core::DisplayRasterLevel shown =
        rasterLevel(view->image().width(), view->image().height(), view->sourceRect());
    const mviewer::core::DisplayRasterLevel want =
        rasterLevel(desired.target.width(), desired.target.height(), desired.sourceRect);
    const int idx = mviewer::core::selectDisplayStandIn(levels.data(), count, shown, want);
    if (idx < 0 || slot.images[static_cast<size_t>(idx)].isNull())
        return false;

    const double oldScale = view->scale();
    const QPointF oldOffset = view->offset();
    const QSize oldSource = view->sourceSize();
    const auto &lvl = slot.levels[static_cast<size_t>(idx)];
    view->setImage(slot.images[static_cast<size_t>(idx)], slot.sourceSize, lvl.sourceRect);
    if (oldSource.isValid() && oldSource == slot.sourceSize)
        view->setTransform(oldScale, oldOffset);
    return true;
}

// The state machine owns the Loading badge; RawImageView only renders it.
void syncSoftLoadingBadges(const mviewer::ui::CompareDisplayStateMachine &state,
                           const QList<RawImageView *> &views)
{
    if (state.paneCount() != views.size())
        return;
    for (int i = 0; i < views.size(); ++i)
    {
        if (views[i])
            views[i]->setSoftLoading(state.badge(i));
    }
}
} // namespace

TaskScheduler::TaskHandle CompareWorkspace::startDisplayMaterialization(
    const std::vector<ImageData> &pixels,
    const std::vector<mviewer::domain::ImageMetadata> &metadata,
    const std::vector<DisplayRequest> &displayRequests, const std::vector<CellAdjust> &adjusts,
    const std::vector<int> &panes, int paneCount, uint64_t gen,
    const std::vector<std::string> &paths, const mviewer::core::DisplayColorContext &target,
    const QPointer<CompareWorkspace> &guard)
{
    bool provisional = false;
    bool preview = false;
    for (const DisplayRequest &req : displayRequests)
    {
        if (req.preview)
            preview = true;
        if (req.provisional || req.preview)
            provisional = true;
    }
    // Cheap provisional paint stays on Decode so Analysis hist/diff cannot
    // starve the first pixels. The sharp HQ batch stays on Analysis: that is
    // the pool session drains wait on, and releaseDisplayTask() drops its
    // handle on generation change, hide, and teardown. Parking HQ on Decode
    // (forceDecodePriority) left the handle alive after those drains.
    const auto priority =
        provisional ? TaskScheduler::Priority::Decode : TaskScheduler::Priority::Analysis;
    return TaskScheduler::instance().submit(
        priority,
        [pixels, metadata, displayRequests, adjusts, panes, paneCount, gen, paths, target, guard,
         provisional, preview](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return;
            DisplayBatchResult result =
                materializeDisplayBatch(pixels, metadata, displayRequests, adjusts, panes,
                                        paneCount, gen, paths, target, ctx);
            if (ctx.isCancelled())
                return;
            result.provisional = provisional;
            result.preview = preview;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result]()
                {
                    CompareWorkspace *ws = guard.data();
                    if (ws)
                        ws->applyDisplayBatchResult(result);
                },
                Qt::QueuedConnection);
        });
}

void CompareWorkspace::releaseDisplayTask()
{
    if (m_displayTask)
        TaskScheduler::cancelTree(m_displayTask->id);
    m_displayTask.reset();
    if (m_session)
        m_session->displayState.abandon();
}

void CompareWorkspace::scheduleDisplayMaterialization(const std::vector<int> &dirtyPanes,
                                                      bool preview)
{
    if (!m_session)
        return;
    const int paneCount = static_cast<int>(m_cellViews.size());
    auto &state = m_session->displayState;
    std::vector<uint8_t> hasRaster(static_cast<size_t>(std::max(0, paneCount)), 0);
    std::vector<uint8_t> badge(hasRaster.size(), 0);
    for (int i = 0; i < paneCount; ++i)
    {
        const RawImageView *view = m_cellViews[i];
        hasRaster[static_cast<size_t>(i)] = (view && !view->image().isNull()) ? 1 : 0;
        badge[static_cast<size_t>(i)] = (view && view->softLoading()) ? 1 : 0;
    }
    if (state.paneCount() != paneCount)
        state.adopt(hasRaster, badge);
    // Every pane that is not Ready / Failed rides along (blank, warm stand-in,
    // cheap or sharp still owed, or carried by the batch cancelled below), so
    // a single-pane request (fast preview, per-pane scaleChanged LOD refresh)
    // can never strand another pane on "Loading" + a low-res raster.
    const mviewer::ui::CompareDisplayBatchPlan plan =
        state.plan(dirtyPanes, hasRaster, m_loadInFlight);
    // Nothing to request: keep the in-flight batch instead of cancelling it.
    if (plan.empty())
        return;

    // Latest-wins: drop any in-flight batch from the scheduler graph and start
    // a fresh generation. Cooperative cancel would keep the handle until the
    // sharp resample returned.
    releaseDisplayTask();
    ++m_displayGen;
    std::vector<int> panes = plan.panes;

    // Stagger priority: focus/edit pane first so first paint is stable.
    int focusPane = m_editIdx;
    if (focusPane < 0)
        focusPane = resolveEditCell();
    if (focusPane >= 0)
    {
        auto it = std::find(panes.begin(), panes.end(), focusPane);
        if (it != panes.end() && it != panes.begin())
            std::iter_swap(panes.begin(), it);
    }

    // Immediate pyramid paint-through when a coarser ready level covers desired.
    for (int idx : panes)
    {
        DisplayRequest desired;
        const ImageFrame *img = m_engine.imageAt(idx);
        if (img && !img->pixels().isNull())
            desired = memoryDisplayRequest(idx, false);
        else if (idx < static_cast<int>(m_comparePaths.size()))
            desired = buildPaneDisplayRequest(idx, false);
        if (!desired.target.isValid())
            continue;
        if (tryPaintFromPyramid(m_session.get(), m_cellViews, idx, paneContentKey(idx), desired))
            tagPaneContent(idx);
    }
    // tryPaintFromPyramid -> setImage() fits the raster. A swap has already
    // stored the destination scale; put it back on the view before paint.
    reapplyPinnedCellTransforms();

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
        // Cheap vs sharp is decided per pane by the state machine: cheap at
        // most once per pane epoch, so cheap -> skip -> cheap cannot recur.
        // Panes outside the batch never mark it provisional.
        const bool cheap = plan.wantsCheap(i);
        if (img && !img->pixels().isNull())
            displayRequests.push_back(memoryDisplayRequest(i, cheap));
        else if (i < static_cast<int>(m_comparePaths.size()) &&
                 !m_comparePaths[static_cast<size_t>(i)].empty())
        {
            displayRequests.push_back(buildPaneDisplayRequest(i, cheap));
        }
        else
        {
            displayRequests.push_back({});
        }
    }
    if (preview)
    {
        for (int idx : panes)
        {
            if (idx < 0 || idx >= static_cast<int>(displayRequests.size()))
                continue;
            if (idx >= static_cast<int>(pixels.size()) || pixels[static_cast<size_t>(idx)].isNull())
                continue;
            displayRequests[static_cast<size_t>(idx)].preview = true;
        }
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    const uint64_t gen = m_displayGen;
    const auto target = m_displayColorTarget;
    QPointer<CompareWorkspace> guard(this);

    auto handle = startDisplayMaterialization(pixels, metadata, displayRequests, adjusts, panes,
                                              paneCount, gen, m_comparePaths, target, guard);
    if (!handle)
        return;
    m_displayTask = handle;
    state.commit(plan, gen);
    if (preview)
        m_session->previewGeneration = gen;
}

void CompareWorkspace::applyDisplayBatchResult(const DisplayBatchResult &r)
{
    // Generation is the primary latest-wins gate. Provisional-over-full is
    // handled per-pane via pyramid haveFull when remembering deliveries.
    if (r.generation != m_displayGen || !m_session)
        return;
    if (r.preview && m_session->previewGeneration == r.generation)
        m_session->previewGeneration = 0;
    if (r.paneCount != static_cast<int>(m_cellViews.size()))
        return;

    m_displayTask.reset();
    if (!r.provisional)
        m_session->forceDecodePriority = false;
    auto &state = m_session->displayState;

    // A sharp raster replacing a preview of the same view does not change the
    // pixels the inspector samples, so it must not cost a second render.
    bool inspectorStale = false;
    for (const auto &cell : r.cells)
    {
        if (cell.index < 0 || cell.index >= static_cast<int>(m_cellViews.size()))
            continue;
        RawImageView *view = m_cellViews[static_cast<size_t>(cell.index)];
        // A pane re-entered after submit (pair switch) no longer expects this
        // raster: it is for the previous source.
        if (!view || !state.expects(cell.index, r.generation))
            continue;
        if (!cell.errorText.isEmpty())
        {
            const QString message = tr("Cannot display this source: %1").arg(cell.errorText);
            // A failed pane will not get a raster from this request; the
            // caption carries the error, so the Loading badge must not stick.
            state.deliver(cell.index, r.generation, mviewer::ui::ComparePaneDelivery::Failed,
                          r.preview);
            view->setToolTip(message);
            if (view->image().isNull() && cell.index < m_cellLabels.size())
                setComparePaneCaptionStatus(m_cellLabels[cell.index], message);
            continue;
        }
        // Per cell, not per batch: a sharp cell riding in a batch with a cheap
        // one must not be dropped by the provisional-over-full guard.
        const bool cheapCell = state.requestedCheap(cell.index, r.generation);
        const QSize oldSize = view->image().size();
        const QSize oldSourceSize = view->sourceSize();
        const double oldScale = view->scale();
        const QPointF oldOffset = view->offset();
        if (skipLateProvisional(cheapCell || r.preview, r.preview, cell.index, view->image(),
                                view->sourceRect(), cell.image, cell.sourceRect, m_session.get()))
        {
            state.deliver(cell.index, r.generation, mviewer::ui::ComparePaneDelivery::Skipped,
                          r.preview);
            continue;
        }
        state.deliver(cell.index, r.generation, mviewer::ui::ComparePaneDelivery::Accepted,
                      r.preview);
        tagPaneContent(cell.index);
        view->setImage(cell.image, cell.sourceSize, cell.sourceRect);
        if (!swapPreviewState(m_session.get(), cell.index, r.preview))
            inspectorStale = true;
        if (cell.index < m_cellLabels.size())
            clearComparePaneCaptionStatus(m_cellLabels[cell.index]);
        if (!oldSize.isEmpty() && view->sourceSize() == oldSourceSize)
            view->setTransform(oldScale, oldOffset);
        if (cell.sourceRect != QRect(QPoint(0, 0), cell.sourceSize))
            view->clearOverlay();

        if (!r.preview && cell.index < static_cast<int>(m_comparePaths.size()))
        {
            DisplayRequest remembered{cell.image.size(), cell.sourceRect,
                                      cell.sourceRect != QRect(QPoint(0, 0), cell.sourceSize),
                                      cheapCell};
            remembered.target = cell.image.size();
            rememberPyramidDelivery(
                m_session.get(), cell.index, m_comparePaths[static_cast<size_t>(cell.index)],
                paneContentKey(cell.index), cell.image, cell.sourceSize, remembered, !cheapCell);
        }
    }

    // setImage() above fitted whatever raster just arrived. Pinned panes are
    // mid-swap: the engine already holds the swapped scale and pan, and the
    // view has to show those even when the source size changed. A provisional
    // delivery stays pinned until the full raster follows.
    reapplyPinnedCellTransforms(!r.provisional);

    if (inspectorStale && m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);

    updateTemporaryCompareAvailability();
    update();
    followUpDisplayBatch(r);
}

void CompareWorkspace::followUpDisplayBatch(const DisplayBatchResult &r)
{
    auto &state = m_session->displayState;
    // Requested panes without a delivered cell count an idle landing (not
    // for a preview). The state machine fails a pane after kMaxIdleLandings
    // landings without progress; that backstop should never fire, so it is
    // logged.
    for (const int pane : state.land(r.generation, r.preview))
    {
        qWarning("CompareWorkspace: pane %d made no display progress in %d batches; "
                 "clearing its Loading badge",
                 pane, mviewer::ui::CompareDisplayStateMachine::kMaxIdleLandings);
    }
    syncSoftLoadingBadges(state, m_cellViews);

    // Progressive pyramid: after a cheap first paint (or a cheap pass skipped
    // as blurrier than the stand-in) the sharp viewport LOD follows at once.
    // After a wheel preview only badged panes follow while the trailing
    // debounce is still armed; once it has fired, nothing else would ask.
    if (state.awaitingRequest(r.preview && m_displayLodRefreshPending).empty())
        return;
    const uint64_t gen = m_displayGen;
    QPointer<CompareWorkspace> guard(this);
    QTimer::singleShot(0, this,
                       [guard, gen]()
                       {
                           CompareWorkspace *ws = guard.data();
                           // A newer batch already carries every pending pane.
                           if (!ws || ws->m_displayGen != gen || ws->m_displayTask)
                               return;
                           ws->scheduleDisplayMaterialization({});
                       });
}

void CompareWorkspace::queueDisplayLod(int requestedPane)
{
    std::vector<int> dirty;
    auto addIfNeeded = [this, &dirty](int pane)
    {
        if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
            return;
        // A pane the state machine still owes a raster (stand-in, cheap pass)
        // always needs a request, even when the stand-in (previous pair, same
        // dimensions) "covers" the view.
        if (m_session && m_session->displayState.needsRequest(pane) && !m_loadInFlight)
        {
            dirty.push_back(pane);
            return;
        }
        const ImageFrame *frame = m_engine.imageAt(pane);
        if (!frame || frame->pixels().isNull())
        {
            if (pane < static_cast<int>(m_comparePaths.size()) &&
                !m_comparePaths[static_cast<size_t>(pane)].empty())
            {
                const RawImageView *view = m_cellViews[pane];
                const DisplayRequest desired = sourceDisplayRequest(pane);
                const QRect current = view->sourceRect();
                const bool hasRaster = !view->image().isNull();
                bool stale = !hasRaster || !current.isValid();
                if (!stale && desired.region)
                {
                    const QRect visible = sourceVisibleRect(pane);
                    const double currentDensity =
                        static_cast<double>(view->image().width()) / std::max(1, current.width());
                    const double requiredDensity = static_cast<double>(desired.target.width()) /
                                                   std::max(1, desired.sourceRect.width());
                    stale = !current.contains(desired.sourceRect) ||
                            currentDensity < requiredDensity * 0.9 || !current.contains(visible);
                }
                else if (!stale)
                {
                    stale = desired.region ||
                            std::max(view->image().width(), view->image().height()) !=
                                std::max(desired.target.width(), desired.target.height()) ||
                            current != desired.sourceRect;
                }
                if (stale)
                    dirty.push_back(pane);
            }
            return;
        }
        const DisplayRequest desired = memoryDisplayRequest(pane, false);
        const bool previewShown = m_session && pane >= 0 &&
                                  static_cast<size_t>(pane) < m_session->displayIsPreview.size() &&
                                  m_session->displayIsPreview[static_cast<size_t>(pane)] != 0;
        if (previewShown || !rasterCoversView(pane, desired))
            dirty.push_back(pane);
    };
    if (requestedPane >= 0)
        addIfNeeded(requestedPane);
    else
    {
        dirty.reserve(static_cast<size_t>(m_cellViews.size()));
        for (int i = 0; i < m_cellViews.size(); ++i)
            addIfNeeded(i);
    }
    // A pending pane outside `requestedPane` (owed its sharp pass after a
    // preview) rides along: the plan carries every pending pane.
    if (!dirty.empty() ||
        (m_session && !m_loadInFlight && !m_session->displayState.awaitingRequest().empty()))
        scheduleDisplayMaterialization(dirty);
}

void CompareWorkspace::queueFastPreview(int requestedPane)
{
    if (!isVisible() || !m_session || m_session->fastPreviewBusy)
        return;
    if (m_session->previewGeneration != 0 && m_session->previewGeneration == m_displayGen &&
        m_displayTask)
        return;
    std::vector<int> dirty;
    auto consider = [this, &dirty](int pane)
    {
        if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
            return;
        const ImageFrame *frame = m_engine.imageAt(pane);
        if (!frame || frame->pixels().isNull())
            return;
        const DisplayRequest desired = memoryDisplayRequest(pane, false);
        if (!desired.target.isValid() || rasterCoversView(pane, desired))
            return;
        const RawImageView *view = m_cellViews[pane];
        const mviewer::core::DisplayRasterLevel shown =
            rasterLevel(view->image().width(), view->image().height(), view->sourceRect());
        const mviewer::core::DisplayRasterLevel want =
            rasterLevel(desired.target.width(), desired.target.height(), desired.sourceRect);
        if (mviewer::core::isBlurrierDisplayStandIn(shown, want))
            return;
        dirty.push_back(pane);
    };
    if (requestedPane >= 0)
        consider(requestedPane);
    else
    {
        for (int i = 0; i < m_cellViews.size(); ++i)
            consider(i);
    }
    if (dirty.empty())
        return;
    struct Busy
    {
        bool &flag;
        explicit Busy(bool &held) : flag(held)
        {
            flag = true;
        }
        ~Busy()
        {
            flag = false;
        }
    } busy(m_session->fastPreviewBusy);
    scheduleDisplayMaterialization(dirty, true);
}

quint64 CompareWorkspace::paneContentKey(int pane) const
{
    if (pane < 0)
        return 0;
    // FNV-1a over the pane's source path and every adjustment that changes
    // pixels or geometry. Collisions only cost a briefly stale pan backdrop.
    quint64 h = 1469598103934665603ULL;
    auto mix = [&h](const void *data, size_t size)
    {
        const auto *bytes = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };
    const auto idx = static_cast<size_t>(pane);
    if (idx < m_comparePaths.size())
        mix(m_comparePaths[idx].data(), m_comparePaths[idx].size());
    if (idx < m_cellAdjusts.size())
    {
        const CellAdjust &a = m_cellAdjusts[idx];
        const float gains[] = {a.contrast, a.gamma, a.rGain, a.bGain};
        const double usmDoubles[] = {a.usmAmount, a.usmRadius};
        const int ints[] = {a.brightness,      a.rotation,
                            a.flipH ? 1 : 0,   a.flipV ? 1 : 0,
                            a.hasCrop ? 1 : 0, a.cropX,
                            a.cropY,           a.cropW,
                            a.cropH,           a.usmEnabled ? 1 : 0,
                            a.usmThreshold,    a.usmLumaOnly ? 1 : 0};
        mix(gains, sizeof(gains));
        mix(usmDoubles, sizeof(usmDoubles));
        mix(ints, sizeof(ints));
    }
    return h;
}

void CompareWorkspace::tagPaneContent(int pane)
{
    if (pane >= 0 && pane < m_cellViews.size() && m_cellViews[pane])
        m_cellViews[pane]->setBackdropKey(paneContentKey(pane));
}
