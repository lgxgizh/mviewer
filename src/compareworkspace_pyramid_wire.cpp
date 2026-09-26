#include "compareworkspace_display_planner.h"
#include "compareworkspace_display_pyramid.h"
#include "compareworkspace_p.h"

#include <QTimer>
#include <algorithm>

namespace
{
void rememberPyramidDelivery(mviewer::ui::CompareSessionRuntime *session, int pane,
                             const std::string &path, const QImage &image, const QSize &sourceSize,
                             const CompareWorkspace::DisplayRequest &req, bool full)
{
    if (!session || pane < 0)
        return;
    if (session->panePyramids.size() <= static_cast<size_t>(pane))
        session->panePyramids.resize(static_cast<size_t>(pane) + 1);
    auto &slot = session->panePyramids[static_cast<size_t>(pane)];
    if (slot.path != path)
    {
        slot = {};
        slot.path = path;
    }
    slot.sourceSize = sourceSize;
    // Replace same-edge entry; otherwise append (bounded).
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

bool tryPaintFromPyramid(CompareWorkspace *ws, int pane,
                         const CompareWorkspace::DisplayRequest &desired)
{
    if (!ws || !ws->m_session || pane < 0 ||
        pane >= static_cast<int>(ws->m_session->panePyramids.size()))
        return false;
    auto &slot = ws->m_session->panePyramids[static_cast<size_t>(pane)];
    if (slot.images.empty() || pane >= ws->m_cellViews.size() || !ws->m_cellViews[pane])
        return false;

    std::vector<mviewer::ui::CompareDisplayPlan> have;
    have.reserve(slot.levels.size());
    for (const auto &lvl : slot.levels)
    {
        mviewer::ui::CompareDisplayPlan plan;
        plan.targetWidth = lvl.target.width();
        plan.targetHeight = lvl.target.height();
        plan.sourceRect = {lvl.sourceRect.x(), lvl.sourceRect.y(), lvl.sourceRect.width(),
                           lvl.sourceRect.height()};
        plan.region = lvl.region;
        have.push_back(plan);
    }
    mviewer::ui::CompareDisplayPlan want;
    want.targetWidth = desired.target.width();
    want.targetHeight = desired.target.height();
    want.sourceRect = {desired.sourceRect.x(), desired.sourceRect.y(), desired.sourceRect.width(),
                       desired.sourceRect.height()};
    want.region = desired.region;

    const int idx = mviewer::ui::selectReadyPyramidLevel(have, want);
    if (idx < 0 || idx >= static_cast<int>(slot.images.size()) ||
        slot.images[static_cast<size_t>(idx)].isNull())
        return false;

    RawImageView *view = ws->m_cellViews[pane];
    const double oldScale = view->scale();
    const QPointF oldOffset = view->offset();
    const QSize oldSource = view->sourceSize();
    const auto &lvl = slot.levels[static_cast<size_t>(idx)];
    view->setImage(slot.images[static_cast<size_t>(idx)], slot.sourceSize, lvl.sourceRect);
    if (oldSource.isValid() && oldSource == slot.sourceSize)
        view->setTransform(oldScale, oldOffset);
    return true;
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
    for (const DisplayRequest &req : displayRequests)
    {
        if (req.provisional)
        {
            provisional = true;
            break;
        }
    }
    // Visible Compare panes (blank/soft/provisional/forced) race Decode so
    // neighbor Background preload and Analysis hist/diff never starve them.
    const bool forceDecode = guard && guard->m_session && guard->m_session->forceDecodePriority;
    const auto priority = (provisional || forceDecode) ? TaskScheduler::Priority::Decode
                                                       : TaskScheduler::Priority::Analysis;
    return TaskScheduler::instance().submit(
        priority,
        [pixels, metadata, displayRequests, adjusts, panes, paneCount, gen, paths, target, guard,
         provisional](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return;
            DisplayBatchResult result =
                materializeDisplayBatch(pixels, metadata, displayRequests, adjusts, panes,
                                        paneCount, gen, paths, target, ctx);
            if (ctx.isCancelled())
                return;
            result.provisional = provisional;
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

void CompareWorkspace::scheduleDisplayMaterialization(const std::vector<int> &dirtyPanes)
{
    // Latest-wins: cancel any in-flight batch and start a fresh generation.
    if (m_displayTask)
        TaskScheduler::cancel(m_displayTask);
    m_displayTask.reset();
    ++m_displayGen;

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
        return;

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
        {
            desired = DisplayRequest{
                displayLodTarget(idx, img->pixels()),
                QRect(QPoint(0, 0), QSize(img->pixels().width, img->pixels().height)), false,
                false};
        }
        else if (idx < static_cast<int>(m_comparePaths.size()))
        {
            desired = buildPaneDisplayRequest(idx, false);
        }
        if (desired.target.isValid())
            tryPaintFromPyramid(this, idx, desired);
    }

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
        RawImageView *view = (i < m_cellViews.size()) ? m_cellViews[i] : nullptr;
        const bool blankPane = !view || view->image().isNull() || view->softLoading();
        if (img && !img->pixels().isNull())
        {
            DisplayRequest req{
                displayLodTarget(i, img->pixels()),
                QRect(QPoint(0, 0), QSize(img->pixels().width, img->pixels().height)), false,
                false};
            if (blankPane)
            {
                const int edge = std::min(640, std::max(req.target.width(), req.target.height()));
                if (edge > 0 && edge < std::max(req.target.width(), req.target.height()))
                {
                    req.target = QSize(edge, edge);
                    req.provisional = true;
                }
            }
            displayRequests.push_back(req);
        }
        else if (i < static_cast<int>(m_comparePaths.size()) &&
                 !m_comparePaths[static_cast<size_t>(i)].empty())
        {
            displayRequests.push_back(buildPaneDisplayRequest(i, blankPane));
        }
        else
        {
            displayRequests.push_back({});
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
}

void CompareWorkspace::applyDisplayBatchResult(const DisplayBatchResult &r)
{
    // Generation is the primary latest-wins gate. Provisional-over-full is
    // handled per-pane via pyramid haveFull when remembering deliveries.
    if (r.generation != m_displayGen)
        return;
    if (r.paneCount != static_cast<int>(m_cellViews.size()))
        return;

    m_displayTask.reset();
    if (!r.provisional && m_session)
        m_session->forceDecodePriority = false;

    for (const auto &cell : r.cells)
    {
        if (cell.index < 0 || cell.index >= static_cast<int>(m_cellViews.size()))
            continue;
        RawImageView *view = m_cellViews[static_cast<size_t>(cell.index)];
        if (!view)
            continue;
        if (!cell.errorText.isEmpty())
        {
            const QString message = tr("无法显示此源：%1").arg(cell.errorText);
            view->setToolTip(message);
            if (view->image().isNull() && cell.index < m_cellLabels.size() &&
                m_cellLabels[cell.index])
            {
                m_cellLabels[cell.index]->setText(message);
                m_cellLabels[cell.index]->setToolTip(message);
            }
            continue;
        }
        const QSize oldSize = view->image().size();
        const QSize oldSourceSize = view->sourceSize();
        const double oldScale = view->scale();
        const QPointF oldOffset = view->offset();
        if (r.provisional && m_session &&
            cell.index < static_cast<int>(m_session->panePyramids.size()) &&
            m_session->panePyramids[static_cast<size_t>(cell.index)].haveFull)
            continue; // do not downgrade a full pane with late provisional
        view->setSoftLoading(false);
        view->setImage(cell.image, cell.sourceSize, cell.sourceRect);
        if (!oldSize.isEmpty() && view->sourceSize() == oldSourceSize)
            view->setTransform(oldScale, oldOffset);
        if (cell.sourceRect != QRect(QPoint(0, 0), cell.sourceSize))
            view->clearOverlay();

        if (m_session && cell.index < static_cast<int>(m_comparePaths.size()))
        {
            DisplayRequest remembered{cell.image.size(), cell.sourceRect,
                                      cell.sourceRect != QRect(QPoint(0, 0), cell.sourceSize),
                                      r.provisional};
            remembered.target = cell.image.size();
            rememberPyramidDelivery(m_session.get(), cell.index,
                                    m_comparePaths[static_cast<size_t>(cell.index)], cell.image,
                                    cell.sourceSize, remembered, !r.provisional);
        }
    }

    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0 && m_lastInspectY >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);

    updateTemporaryCompareAvailability();
    update();

    // Progressive pyramid: after a cheap first paint, upgrade to viewport LOD.
    if (r.provisional && r.generation == m_displayGen)
    {
        std::vector<int> upgrade;
        upgrade.reserve(r.cells.size());
        for (const auto &cell : r.cells)
        {
            if (cell.index >= 0 && cell.errorText.isEmpty())
                upgrade.push_back(cell.index);
        }
        if (!upgrade.empty())
        {
            const uint64_t gen = m_displayGen;
            QPointer<CompareWorkspace> guard(this);
            QTimer::singleShot(0, this,
                               [guard, upgrade, gen]()
                               {
                                   CompareWorkspace *ws = guard.data();
                                   if (!ws || ws->m_displayGen != gen)
                                       return;
                                   ws->scheduleDisplayMaterialization(upgrade);
                               });
        }
    }
}
