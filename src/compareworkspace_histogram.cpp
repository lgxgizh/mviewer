// Histogram computation for CompareWorkspace: title text, refresh scheduling and
// the batched background worker that publishes per-pane histograms.
//
// Extracted from compareworkspace_analysis.cpp (941 lines against the 1000-line
// responsibility ceiling). Behaviour-preserving: the methods keep their
// declarations in compareworkspace.h.
#include "compareworkspace_p.h"

#include <QString>

#include <algorithm>
#include <vector>

QString CompareWorkspace::histogramTitleText(bool roiEnabled,
                                             const mviewer::domain::Selection &roi) const
{
    if (roiEnabled && !roi.isEmpty())
        return tr("Histogram (ROI %1,%2 %3×%4)")
            .arg(roi.x)
            .arg(roi.y)
            .arg(roi.width)
            .arg(roi.height);
    return tr("Histogram (full image)");
}

void CompareWorkspace::refreshHistograms()
{
    if (!m_hist)
        return;

    // One includeMain batch; when the per-pane overlay is enabled every pane
    // joins the same request so the main surface and the overlays share the
    // computed histograms (no duplicate adjusted-pixel/histogram work).
    std::vector<int> panes;
    if (m_paneHistOverlay)
    {
        panes.reserve(m_cellHists.size());
        for (size_t i = 0; i < m_cellHists.size(); ++i)
            panes.push_back(static_cast<int>(i));
    }
    scheduleHistogramRefresh(true, panes);
}

// Schedule one cancellable, latest-wins Analysis-pool task that computes the
// adjusted/ROI-aware histogram for the union of the main-required and
// pane-overlay-required indices. All state the worker needs is snapped by
// value on the UI thread; the worker never touches `this` or any QObject.
void CompareWorkspace::clearMainHistogramForEmptyPlan(bool updateMain)
{
    if (!(updateMain && m_hist))
        return;
    if (m_histTitle)
    {
        const bool useRoi = m_roiHistChk && m_roiHistChk->isChecked() && !m_lastSelection.isEmpty();
        m_histTitle->setText(histogramTitleText(useRoi, m_lastSelection));
    }
    m_hist->setHistograms({});
}

CompareWorkspace::HistIndexPlan
CompareWorkspace::collectHistogramIndices(bool includeMain, const std::vector<int> &paneIndices)
{
    HistIndexPlan plan;
    const int paneCount = static_cast<int>(m_cellViews.size());
    plan.updateMain = includeMain && m_hist;
    if (plan.updateMain)
    {
        if (m_perPaneHist && m_editIdx >= 0 && m_editIdx < paneCount)
            plan.mainIndices.push_back(m_editIdx);
        else
            for (int i = 0; i < paneCount; ++i)
                plan.mainIndices.push_back(i);
    }

    auto addUnique = [paneCount](std::vector<int> &vec, int idx)
    {
        if (idx >= 0 && idx < paneCount && std::find(vec.cbegin(), vec.cend(), idx) == vec.cend())
            vec.push_back(idx);
    };
    for (int idx : paneIndices)
        addUnique(plan.panes, idx);
    if (m_paneHistOverlay)
    {
        const int histCount = static_cast<int>(m_cellHists.size());
        for (int i = 0; i < paneCount && i < histCount; ++i)
            if (m_cellHists[static_cast<size_t>(i)] &&
                m_cellHists[static_cast<size_t>(i)]->histogramCount() == 0)
                addUnique(plan.panes, i);
    }

    plan.unionIdx.reserve(plan.mainIndices.size() + plan.panes.size());
    for (int idx : plan.mainIndices)
        addUnique(plan.unionIdx, idx);
    for (int idx : plan.panes)
        addUnique(plan.unionIdx, idx);
    return plan;
}

void CompareWorkspace::computeHistogramBatch(
    const std::vector<ImageData> &pixels, const std::vector<CellAdjust> &adjusts, bool roiEnabled,
    const std::vector<mviewer::domain::Selection> &rois, int titlePane,
    const std::vector<int> &unionIdx, const std::vector<int> &mainIndices,
    const std::vector<int> &panes, int paneCount, bool updateMain, uint64_t gen,
    const QPointer<CompareWorkspace> &guard, const TaskScheduler::TaskContext &ctx)
{
    if (ctx.isCancelled())
        return;

    HistogramBatchResult r;
    r.generation = gen;
    r.paneCount = paneCount;
    r.updateMain = updateMain;
    r.roiPane = titlePane;
    if (titlePane >= 0 && titlePane < static_cast<int>(rois.size()))
    {
        r.roi = rois[static_cast<size_t>(titlePane)];
        r.roiEnabled = !r.roi.isEmpty();
    }
    else if (titlePane == -2)
    {
        r.roiEnabled = roiEnabled;
        r.roi = {};
    }
    else
    {
        r.roiEnabled = roiEnabled;
        r.roi = rois.empty() ? mviewer::domain::Selection{} : rois.front();
    }

    const auto adjustFor = [&adjusts](int idx) -> CellAdjust
    {
        if (idx >= 0 && idx < static_cast<int>(adjusts.size()))
            return adjusts[idx];
        return CellAdjust{};
    };

    std::vector<HistogramBatchResult::CellHist> computed;
    computed.reserve(unionIdx.size());
    for (int idx : unionIdx)
    {
        if (ctx.isCancelled())
            return;
        if (idx < 0 || idx >= static_cast<int>(pixels.size()))
            continue;
        const ImageData &src = pixels[static_cast<size_t>(idx)];
        if (src.isNull())
            continue;
        const ImageData adjusted = CompareWorkspace::applyAdjusts(src, adjustFor(idx), [&ctx]()
                                                                  { return ctx.isCancelled(); });
        if (ctx.isCancelled())
            return;
        if (adjusted.isNull())
            continue;
        const mviewer::domain::Selection paneRoi = idx >= 0 && idx < static_cast<int>(rois.size())
                                                       ? rois[static_cast<size_t>(idx)]
                                                       : mviewer::domain::Selection{};
        // No selection on this pane: count the whole image.
        mviewer::core::Histogram h =
            paneRoi.isEmpty() ? mviewer::core::computeDisplayHistogram(adjusted)
                              : mviewer::core::computeHistogram(adjusted, paneRoi.x, paneRoi.y,
                                                                paneRoi.width, paneRoi.height);
        if (ctx.isCancelled())
            return;
        HistogramBatchResult::CellHist cell;
        cell.index = idx;
        cell.hist = std::move(h);
        computed.push_back(std::move(cell));
    }

    if (ctx.isCancelled())
        return;

    if (r.updateMain)
    {
        r.main.reserve(mainIndices.size());
        r.mainIndices.reserve(mainIndices.size());
        for (int idx : mainIndices)
        {
            const auto it = std::find_if(computed.cbegin(), computed.cend(),
                                         [idx](const HistogramBatchResult::CellHist &c)
                                         { return c.index == idx; });
            if (it != computed.cend())
            {
                r.main.push_back(it->hist);
                r.mainIndices.push_back(idx);
            }
        }
    }
    for (int idx : panes)
    {
        const auto it =
            std::find_if(computed.cbegin(), computed.cend(),
                         [idx](const HistogramBatchResult::CellHist &c) { return c.index == idx; });
        if (it == computed.cend())
            continue;
        r.panes.push_back(*it);
    }

    if (ctx.isCancelled())
        return;

    QMetaObject::invokeMethod(
        qApp,
        [guard, r]()
        {
            CompareWorkspace *ws = guard.data();
            if (!ws)
                return;
            ws->applyHistogramBatchResult(r);
        },
        Qt::QueuedConnection);
}

void CompareWorkspace::scheduleHistogramRefresh(bool includeMain,
                                                const std::vector<int> &paneIndices)
{
    // rebuildCells must always submit its terminal hist batch; deferral is only
    // for user zoom/pan/soft-load, not structural rebuilds.
    if (!m_rebuildingCells && shouldDeferHeavyCompareWork())
    {
        m_deferredHistRefresh = true;
        return;
    }

    // Latest-wins: cancel any in-flight batch and start a fresh generation.
    if (m_histTask)
        TaskScheduler::cancel(m_histTask);
    m_histTask.reset();
    ++m_histGen;

    const HistIndexPlan plan = collectHistogramIndices(includeMain, paneIndices);
    if (plan.unionIdx.empty())
    {
        clearMainHistogramForEmptyPlan(plan.updateMain);
        return;
    }

    const int paneCount = static_cast<int>(m_cellViews.size());
    std::vector<ImageData> pixels;
    pixels.reserve(static_cast<size_t>(paneCount));
    for (int i = 0; i < paneCount; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        pixels.push_back(img ? img->pixels() : ImageData());
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    // Linked panes share m_lastSelection. Unlinked panes each keep the
    // rectangle on that view; an empty selection is not a crop.
    const bool roiToggle = m_roiHistChk && m_roiHistChk->isChecked();
    const bool linkedRoi = linkedROIAvailable();
    std::vector<mviewer::domain::Selection> rois(static_cast<size_t>(paneCount));
    if (roiToggle)
    {
        if (linkedRoi || m_sharedUnlinkedRoi)
        {
            if (!m_lastSelection.isEmpty())
            {
                for (mviewer::domain::Selection &paneRoi : rois)
                    paneRoi = m_lastSelection;
            }
        }
        else
        {
            for (int i = 0; i < paneCount && i < m_cellViews.size(); ++i)
            {
                if (m_cellViews[i])
                    rois[static_cast<size_t>(i)] = m_cellViews[i]->selection();
            }
        }
    }
    bool anyRoi = false;
    for (const mviewer::domain::Selection &paneRoi : rois)
    {
        if (!paneRoi.isEmpty())
        {
            anyRoi = true;
            break;
        }
    }
    // -1 shared title, -2 per-pane title, >=0 the one pane the main histogram shows.
    int titlePane = -1;
    if (anyRoi && !linkedRoi && !m_sharedUnlinkedRoi)
        titlePane = plan.mainIndices.size() == 1 ? plan.mainIndices.front() : -2;
    const bool roiEnabled = anyRoi;
    const uint64_t gen = m_histGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = TaskScheduler::instance().submit(
        deferredAnalysisPriority(),
        [pixels = std::move(pixels), adjusts = std::move(adjusts), roiEnabled,
         rois = std::move(rois), titlePane, unionIdx = plan.unionIdx,
         mainIndices = plan.mainIndices, panes = plan.panes, paneCount,
         updateMain = plan.updateMain, gen, guard](const TaskScheduler::TaskContext &ctx)
        {
            computeHistogramBatch(pixels, adjusts, roiEnabled, rois, titlePane, unionIdx,
                                  mainIndices, panes, paneCount, updateMain, gen, guard, ctx);
        });
    if (!handle)
        return;
    m_histTask = handle;
}

void CompareWorkspace::applyHistogramBatchResult(const HistogramBatchResult &r)
{
    if (r.generation != m_histGen)
        return; // superseded by a newer batch
    if (r.paneCount != static_cast<int>(m_cellViews.size()))
        return; // the pane layout changed while the batch was in flight

    // This is the current generation's terminal delivery: release the handle.
    m_histTask.reset();

    if (r.updateMain && m_hist)
    {
        // Deliver the title and the histogram data as one coherent pair from
        // the value-only state captured at schedule time — never a new ROI
        // title over stale data. An empty main result clears the current
        // histogram (zero-image / no-valid-source current state).
        if (m_histTitle)
        {
            QString title;
            if (r.roiPane == -2 && r.roiEnabled)
                title = tr("Histogram (per-pane ROI)");
            else if (r.roiPane >= 0 && r.roiEnabled && !r.roi.isEmpty())
                title = tr("Histogram (pane %1 ROI %2,%3 %4×%5)")
                            .arg(QChar('A' + r.roiPane))
                            .arg(r.roi.x)
                            .arg(r.roi.y)
                            .arg(r.roi.width)
                            .arg(r.roi.height);
            else
                title = histogramTitleText(r.roiEnabled, r.roi);
            QString tip = tr("Histogram and dynamic range clipping analysis");
            if (!r.main.empty() && r.main[0].total > 0 && r.main[0].bins == 256)
            {
                const auto &h0 = r.main[0];
                const double under =
                    (100.0 * static_cast<double>(h0.luma[0])) / static_cast<double>(h0.total);
                const double over =
                    (100.0 * static_cast<double>(h0.luma[255])) / static_cast<double>(h0.total);
                title += tr(" · Clipping: dark %1% / bright %2%")
                             .arg(QString::number(under, 'f', under >= 0.1 ? 1 : 2),
                                  QString::number(over, 'f', over >= 0.1 ? 1 : 2));
                QStringList tipLines;
                for (size_t i = 0; i < r.main.size(); ++i)
                {
                    const auto &h = r.main[i];
                    if (h.total <= 0 || h.bins < 256)
                        continue;
                    const int slot =
                        i < r.mainIndices.size() ? r.mainIndices[i] : static_cast<int>(i);
                    const double u =
                        (100.0 * static_cast<double>(h.luma[0])) / static_cast<double>(h.total);
                    const double o =
                        (100.0 * static_cast<double>(h.luma[255])) / static_cast<double>(h.total);
                    tipLines << tr("[%1] pixels %2 · shadows (Y=0): %3 (%4%) · highlights (Y=255): "
                                   "%5 (%6%)")
                                    .arg(QChar('A' + slot))
                                    .arg(static_cast<qlonglong>(h.total))
                                    .arg(static_cast<qlonglong>(h.luma[0]))
                                    .arg(QString::number(u, 'f', 2))
                                    .arg(static_cast<qlonglong>(h.luma[255]))
                                    .arg(QString::number(o, 'f', 2));
                }
                if (!tipLines.isEmpty())
                    tip = tipLines.join(QLatin1Char('\n'));
            }
            m_histTitle->setText(title);
            m_histTitle->setToolTip(tip);
        }
        m_hist->setHistograms(r.main);
    }

    // Pane results are per image; the pane widgets show whichever image their
    // pane presents right now (a temporary switch shows the other image's).
    if (!r.panes.empty())
    {
        if (m_paneHistCache.size() < m_cellHists.size())
            m_paneHistCache.resize(m_cellHists.size());
        for (const auto &cell : r.panes)
            if (cell.index >= 0 && cell.index < static_cast<int>(m_paneHistCache.size()))
                m_paneHistCache[static_cast<size_t>(cell.index)] = cell.hist;
        applyDisplayedPaneOverlays();
    }

    update();
}