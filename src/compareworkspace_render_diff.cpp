#include "compareworkspace_p.h"

#include <algorithm>
#include <cmath>
CompareWorkspace::DiffSources CompareWorkspace::buildDiffOverlays(
    DiffBatchResult &result, const std::vector<ImageData> &pixels,
    const std::vector<QSize> &displayTargets, const std::vector<CellAdjust> &adjusts,
    int baseIndex, uint8_t threshold, bool highlight, bool visualize,
    const ImageData &basePixels, const TaskScheduler::TaskContext &context)
{
    DiffSources sources;
    result.overlays.reserve(pixels.size());
    const auto adjustFor = [&adjusts](int index) -> CellAdjust
    {
        if (index >= 0 && index < static_cast<int>(adjusts.size()))
            return adjusts[static_cast<size_t>(index)];
        return CellAdjust{};
    };
    for (int i = 0; i < static_cast<int>(pixels.size()); ++i)
    {
        if (context.isCancelled())
            return sources;
        DiffBatchResult::CellOverlay overlay;
        overlay.index = i;
        if (i == baseIndex)
        {
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        if (sources.targetIndex < 0)
            sources.targetIndex = i;
        const ImageData target = CompareWorkspace::applyAdjusts(pixels[i], adjustFor(i));
        if (context.isCancelled())
            return sources;
        if (target.isNull())
        {
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        if (target.width != basePixels.width || target.height != basePixels.height)
        {
            overlay.sizeMismatch = true;
            if (sources.targetIndex == i)
            {
                sources.target = target;
                sources.sizeMismatch = true;
            }
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        const ImageData diff = DifferenceEngine::differenceMap(target, basePixels);
        if (context.isCancelled())
            return sources;
        if (diff.isNull())
        {
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        if (sources.targetIndex == i)
        {
            sources.target = target;
            sources.diff = diff;
        }
        if (visualize)
        {
            const ImageData thresholded = DifferenceEngine::applyThreshold(diff, threshold);
            const ImageData overlayImage =
                highlight ? DifferenceEngine::highlightMap(thresholded, basePixels, threshold)
                          : DifferenceEngine::heatMap(thresholded);
            if (context.isCancelled())
                return sources;
            if (!overlayImage.isNull())
            {
                ImageData displayOverlay = overlayImage;
                const QSize targetSize = displayTargets[static_cast<size_t>(i)];
                if (targetSize.isValid() &&
                    (targetSize.width() < overlayImage.width ||
                     targetSize.height() < overlayImage.height))
                {
                    const double factor = std::min(
                        static_cast<double>(targetSize.width()) / pixels[i].width,
                        static_cast<double>(targetSize.height()) / pixels[i].height);
                    const QSize overlayTarget(
                        std::max(1, static_cast<int>(std::ceil(overlayImage.width * factor))),
                        std::max(1, static_cast<int>(std::ceil(overlayImage.height * factor))));
                    displayOverlay = RenderEngine::scaleBoundedStatic(
                        overlayImage, RenderSize{overlayTarget.width(), overlayTarget.height()});
                }
                overlay.overlay = mvcore::toQImage(displayOverlay);
                overlay.opacity = highlight ? 0.75 : 0.5;
            }
        }
        result.overlays.push_back(std::move(overlay));
    }
    return sources;
}

void CompareWorkspace::computeDiffMetrics(DiffBatchResult &result, const DiffSources &sources,
                                          const ImageData &basePixels, uint8_t threshold,
                                          const mviewer::domain::Selection &roi,
                                          const TaskScheduler::TaskContext &context)
{
    if (sources.targetIndex < 0)
        return;
    result.targetIdx = sources.targetIndex;
    if (sources.target.isNull())
        return;
    if (sources.sizeMismatch)
    {
        result.sizeMismatch = true;
        return;
    }
    if (sources.diff.isNull())
        return;
    if (context.isCancelled())
        return;
    result.psnr = AnalysisEngine::psnr(basePixels, sources.target);
    if (context.isCancelled())
        return;
    result.ssim = AnalysisEngine::ssim(basePixels, sources.target);
    result.metricsValid = true;
    if (context.isCancelled())
        return;
    result.stats = DifferenceEngine::computeStats(sources.diff, threshold);
    result.hasStats = true;
    if (!roi.isEmpty())
    {
        if (context.isCancelled())
            return;
        result.roiStats = DifferenceEngine::computeStats(
            sources.diff, threshold, roi.x, roi.y, roi.width, roi.height);
        result.hasRoiStats = result.roiStats.totalPixels > 0;
    }
}

CompareWorkspace::DiffBatchResult CompareWorkspace::computeDiffBatch(
    const std::vector<ImageData> &pixels, const std::vector<QSize> &displayTargets,
    const std::vector<CellAdjust> &adjusts, int baseIndex, uint8_t threshold, bool highlight,
    bool visualize, const mviewer::domain::Selection &roi, int paneCount, uint64_t generation,
    const TaskScheduler::TaskContext &context)
{
    DiffBatchResult result;
    result.generation = generation;
    result.baseIdx = baseIndex;
    const auto adjustFor = [&adjusts](int index) -> CellAdjust
    {
        if (index >= 0 && index < static_cast<int>(adjusts.size()))
            return adjusts[static_cast<size_t>(index)];
        return CellAdjust{};
    };
    const ImageData basePixels = CompareWorkspace::applyAdjusts(
        pixels[static_cast<size_t>(baseIndex)], adjustFor(baseIndex));
    if (context.isCancelled())
        return result;
    if (basePixels.isNull())
    {
        for (int i = 0; i < paneCount; ++i)
        {
            DiffBatchResult::CellOverlay overlay;
            overlay.index = i;
            result.overlays.push_back(std::move(overlay));
        }
        return result;
    }
    const DiffSources sources = buildDiffOverlays(
        result, pixels, displayTargets, adjusts, baseIndex, threshold, highlight, visualize,
        basePixels, context);
    if (!context.isCancelled())
        computeDiffMetrics(result, sources, basePixels, threshold, roi, context);
    return result;
}

TaskScheduler::TaskHandle CompareWorkspace::startDiffBatch(
    const std::vector<ImageData> &pixels, const std::vector<QSize> &displayTargets,
    const std::vector<CellAdjust> &adjusts, int baseIndex, uint8_t threshold, bool highlight,
    bool visualize, const mviewer::domain::Selection &roi, int paneCount, uint64_t generation,
    const QPointer<CompareWorkspace> &guard)
{
    return TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, displayTargets, adjusts, baseIndex, threshold, highlight, visualize, roi, paneCount,
         generation, guard](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled())
                return;
            const DiffBatchResult result = CompareWorkspace::computeDiffBatch(
                pixels, displayTargets, adjusts, baseIndex, threshold, highlight, visualize, roi,
                paneCount, generation, context);
            if (context.isCancelled())
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result]()
                {
                    CompareWorkspace *workspace = guard.data();
                    if (workspace)
                        workspace->applyDiffBatchResult(result);
                },
                Qt::QueuedConnection);
        });
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

    // Visibility is a product state independent of metrics. Turning the
    // visualization off restores every source image immediately; the batch
    // below still computes PSNR/SSIM/stats and its generation prevents an old
    // in-flight heatmap from reappearing.
    if (!m_diffOverlayVisible)
    {
        for (RawImageView *view : m_cellViews)
            if (view)
                view->setOverlay(QImage(), 0.0);
    }

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
    std::vector<QSize> displayTargets;
    pixels.reserve(paneCount);
    displayTargets.reserve(paneCount);
    for (int i = 0; i < paneCount; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        pixels.push_back(img ? img->pixels() : ImageData());
        displayTargets.push_back(img ? displayLodTarget(i, img->pixels()) : QSize());
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    const uint8_t threshold = m_thresholdValue;
    const bool highlight = m_diffHighlight;
    const bool visualize = m_diffOverlayVisible;
    const mviewer::domain::Selection roi = m_lastSelection;
    const uint64_t gen = m_diffGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = startDiffBatch(
        pixels, displayTargets, adjusts, baseIdx, threshold, highlight, visualize, roi,
        paneCount, gen, guard);
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


