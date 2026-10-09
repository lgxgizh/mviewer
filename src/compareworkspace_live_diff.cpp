#include "compare_live_diff.h"
#include "compareworkspace_p.h"

#include <QApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>

namespace
{

std::vector<ImageData> downscalePixelsForLive(const std::vector<ImageData> &pixels, int maxEdge)
{
    std::vector<ImageData> out;
    out.reserve(pixels.size());
    for (const auto &p : pixels)
        out.push_back(mviewer::ui::downscaleForLiveDiff(p, maxEdge));
    return out;
}

} // namespace

void CompareWorkspace::scheduleLiveDiffPreview()
{
    if (!m_diffOverlayVisible)
        return;
    const int paneCount = m_cellViews.size();
    if (paneCount < 2)
        return;

    // Latest-wins against any in-flight full or prior live batch.
    if (m_diffTask)
        TaskScheduler::cancel(m_diffTask);
    m_diffTask.reset();
    ++m_diffGen;

    const int baseIdx = std::clamp(diffBaseIndex(), 0, paneCount - 1);
    std::vector<ImageData> pixels;
    pixels.reserve(paneCount);
    for (int i = 0; i < paneCount; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        pixels.push_back(img ? img->pixels() : ImageData());
    }
    std::vector<CellAdjust> adjusts = m_cellAdjusts;
    const uint8_t threshold = m_thresholdValue;
    const double gain = m_diffGain;
    const bool highlight = m_diffHighlight;
    const uint64_t gen = m_diffGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, adjusts, baseIdx, threshold, gain, highlight, paneCount, gen,
         guard](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled())
                return;

            const std::vector<ImageData> livePixels =
                downscalePixelsForLive(pixels, mviewer::ui::kLiveDiffMaxEdge);
            if (context.isCancelled())
                return;

            // Live preview: no auto-align, no PSNR/SSIM/ROI metrics — overlay only.
            std::vector<QSize> displayTargets(livePixels.size());
            for (size_t i = 0; i < livePixels.size(); ++i)
            {
                if (!livePixels[i].isNull())
                    displayTargets[i] = QSize(livePixels[i].width, livePixels[i].height);
            }
            DiffBatchResult result = CompareWorkspace::computeDiffBatch(
                livePixels, displayTargets, adjusts, baseIdx, threshold, gain, highlight,
                /*visualize=*/true, /*autoAlign=*/false, mviewer::domain::Selection{}, paneCount,
                gen, context);
            result.provisional = true;
            // Drop expensive metrics even if computeDiffBatch filled them from
            // the cheap rasters — settle will replace with full-precision.
            result.metricsValid = false;
            result.hasStats = false;
            result.hasRoiStats = false;
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
    if (handle)
        m_diffTask = handle;
}
