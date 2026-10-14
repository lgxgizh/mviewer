#include "compareworkspace_p.h"

#include "core/compare/Aligner.h"

#include <QCheckBox>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace
{

// Threshold drags (every ~35 ms) used to redo the adjust copy, alignment,
// difference map, PSNR and SSIM on every tick although none of them depend on
// the threshold. Those threshold-independent products are cached here, keyed
// by pixel-buffer identity plus the exact adjustment bytes and alignment flag.
// Entries keep their source buffers alive, so a cached buffer address can
// never be reused by a different image while its key is still present.
struct AdjustedEntry
{
    std::shared_ptr<std::vector<uint8_t>> sourceHold;
    std::string key;
    ImageData adjusted;
};

struct PairEntry
{
    std::shared_ptr<std::vector<uint8_t>> baseHold;
    std::shared_ptr<std::vector<uint8_t>> targetHold;
    std::string key;
    mviewer::cw::DiffPair pair;
};

// Small caps: a 24 MP pair holds a target and a diff map (~150 MB).
constexpr std::size_t kMaxAdjusted = 2;
constexpr std::size_t kMaxPairs = 4;

std::mutex g_diffCacheMutex;
std::deque<AdjustedEntry> g_adjusted; // front = most recent
std::deque<PairEntry> g_pairs;

std::string identityKey(const ImageData &image)
{
    std::string key(sizeof(const void *) + 3 * sizeof(int), '\0');
    const void *ptr = image.buffer.get();
    const int dims[3] = {image.width, image.height, static_cast<int>(image.format)};
    std::memcpy(key.data(), static_cast<const void *>(&ptr), sizeof(ptr));
    std::memcpy(key.data() + sizeof(ptr), dims, sizeof(dims));
    return key;
}

template <typename Entry> const Entry *findEntry(std::deque<Entry> &entries, const std::string &key)
{
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&key](const Entry &e) { return e.key == key; });
    if (it == entries.end())
        return nullptr;
    if (it != entries.begin())
    {
        Entry moved = std::move(*it);
        entries.erase(it);
        entries.push_front(std::move(moved));
    }
    return &entries.front();
}

template <typename Entry> void storeEntry(std::deque<Entry> &entries, Entry entry, std::size_t cap)
{
    entries.push_front(std::move(entry));
    while (entries.size() > cap)
        entries.pop_back();
}

} // namespace

std::string CompareWorkspace::adjustKey(const CellAdjust &a)
{
    std::string key;
    const auto put = [&key](const auto &value)
    {
        const auto *bytes = reinterpret_cast<const char *>(&value);
        key.append(bytes, sizeof(value));
    };
    put(a.brightness);
    put(a.contrast);
    put(a.gamma);
    put(a.rGain);
    put(a.bGain);
    put(a.rotation);
    put(a.flipH);
    put(a.flipV);
    put(a.hasCrop);
    put(a.cropX);
    put(a.cropY);
    put(a.cropW);
    put(a.cropH);
    return key;
}

ImageData CompareWorkspace::cachedAdjusted(const ImageData &src, const CellAdjust &adjust)
{
    if (src.isNull())
        return CompareWorkspace::applyAdjusts(src, adjust);
    const std::string key = identityKey(src) + adjustKey(adjust);
    {
        const std::lock_guard<std::mutex> lock(g_diffCacheMutex);
        if (const AdjustedEntry *hit = findEntry(g_adjusted, key))
            return hit->adjusted;
    }
    ImageData adjusted = CompareWorkspace::applyAdjusts(src, adjust);
    const std::lock_guard<std::mutex> lock(g_diffCacheMutex);
    storeEntry(g_adjusted, AdjustedEntry{src.buffer, key, adjusted}, kMaxAdjusted);
    return adjusted;
}

// Everything about one (base, target) pair that does not depend on the
// threshold. Returns false when cancelled part-way (nothing is cached then).
bool CompareWorkspace::computeDiffPair(mviewer::cw::DiffPair &out, const ImageData &source,
                                       const CellAdjust &adjust, const ImageData &basePixels,
                                       bool autoAlign, const TaskScheduler::TaskContext &context)
{
    out.target = CompareWorkspace::applyAdjusts(source, adjust);
    if (context.isCancelled())
        return false;
    if (out.target.isNull())
        return true;
    if (out.target.width != basePixels.width || out.target.height != basePixels.height)
    {
        out.sizeMismatch = true;
        return true;
    }
    if (autoAlign)
    {
        const mviewer::AlignOffset off = mviewer::Aligner::estimate(basePixels, out.target, 32);
        out.alignX = off.x;
        out.alignY = off.y;
        if (off.x != 0 || off.y != 0)
            out.target = mviewer::Aligner::shift(out.target, off.x, off.y);
        if (context.isCancelled())
            return false;
    }
    out.diff = DifferenceEngine::differenceMap(out.target, basePixels);
    if (context.isCancelled())
        return false;
    if (out.diff.isNull())
        return true;
    out.psnr = AnalysisEngine::psnr(basePixels, out.target);
    out.ssim = AnalysisEngine::ssim(basePixels, out.target);
    return !context.isCancelled();
}

bool CompareWorkspace::cachedDiffPair(mviewer::cw::DiffPair &out, const ImageData &source,
                                      const CellAdjust &adjust, const ImageData &basePixels,
                                      bool autoAlign, const TaskScheduler::TaskContext &context)
{
    const std::string key =
        identityKey(basePixels) + identityKey(source) + adjustKey(adjust) + (autoAlign ? "A" : "-");
    {
        const std::lock_guard<std::mutex> lock(g_diffCacheMutex);
        if (const PairEntry *hit = findEntry(g_pairs, key))
        {
            out = hit->pair;
            return true;
        }
    }
    if (!computeDiffPair(out, source, adjust, basePixels, autoAlign, context))
        return false;
    if (source.isNull() || basePixels.isNull())
        return true;
    const std::lock_guard<std::mutex> lock(g_diffCacheMutex);
    storeEntry(g_pairs, PairEntry{basePixels.buffer, source.buffer, key, out}, kMaxPairs);
    return true;
}

void CompareWorkspace::clearDiffCache()
{
    const std::lock_guard<std::mutex> lock(g_diffCacheMutex);
    g_adjusted.clear();
    g_pairs.clear();
}
CompareWorkspace::DiffSources CompareWorkspace::buildDiffOverlays(
    DiffBatchResult &result, const std::vector<ImageData> &pixels,
    const std::vector<QSize> &displayTargets, const std::vector<CellAdjust> &adjusts, int baseIndex,
    uint8_t threshold, double gain, bool highlight, bool visualize, bool autoAlign,
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
        mviewer::cw::DiffPair pair;
        if (!cachedDiffPair(pair, pixels[static_cast<size_t>(i)], adjustFor(i), basePixels,
                            autoAlign, context) ||
            context.isCancelled())
            return sources;
        const ImageData &target = pair.target;
        if (target.isNull())
        {
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        if (pair.sizeMismatch)
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
        if (autoAlign)
        {
            result.aligned = true;
            result.alignX = pair.alignX;
            result.alignY = pair.alignY;
        }
        const ImageData &diff = pair.diff;
        if (diff.isNull())
        {
            result.overlays.push_back(std::move(overlay));
            continue;
        }
        overlay.psnr = pair.psnr;
        overlay.ssim = pair.ssim;
        overlay.hasMetrics = true;
        if (sources.targetIndex == i)
        {
            sources.target = target;
            sources.diff = diff;
            sources.psnr = overlay.psnr;
            sources.ssim = overlay.ssim;
            sources.hasMetrics = true;
        }
        if (visualize)
        {
            const ImageData overlayImage =
                DifferenceEngine::visualizeOverlay(diff, basePixels, threshold, gain, highlight);
            if (context.isCancelled())
                return sources;
            if (!overlayImage.isNull())
            {
                ImageData displayOverlay = overlayImage;
                const QSize targetSize = displayTargets[static_cast<size_t>(i)];
                if (targetSize.isValid() && (targetSize.width() < overlayImage.width ||
                                             targetSize.height() < overlayImage.height))
                {
                    const double factor =
                        std::min(static_cast<double>(targetSize.width()) / pixels[i].width,
                                 static_cast<double>(targetSize.height()) / pixels[i].height);
                    const QSize overlayTarget(
                        std::max(1, static_cast<int>(std::ceil(overlayImage.width * factor))),
                        std::max(1, static_cast<int>(std::ceil(overlayImage.height * factor))));
                    displayOverlay = RenderEngine::scaleBoundedStatic(
                        overlayImage, RenderSize{overlayTarget.width(), overlayTarget.height()});
                }
                overlay.overlay = mvcore::toQImageFast(displayOverlay);
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
    if (sources.hasMetrics)
    {
        result.psnr = sources.psnr;
        result.ssim = sources.ssim;
        result.metricsValid = true;
    }
    else
    {
        result.psnr = AnalysisEngine::psnr(basePixels, sources.target);
        if (context.isCancelled())
            return;
        result.ssim = AnalysisEngine::ssim(basePixels, sources.target);
        result.metricsValid = true;
    }
    if (context.isCancelled())
        return;
    result.stats = DifferenceEngine::computeStats(sources.diff, threshold);
    result.hasStats = true;
    result.suggestedThreshold = DifferenceEngine::suggestThreshold(sources.diff);
    result.hasSuggestedThreshold = true;
    if (!roi.isEmpty())
    {
        if (context.isCancelled())
            return;
        result.roiStats = DifferenceEngine::computeStats(sources.diff, threshold, roi.x, roi.y,
                                                         roi.width, roi.height);
        result.hasRoiStats = result.roiStats.totalPixels > 0;
    }
}

CompareWorkspace::DiffBatchResult CompareWorkspace::computeDiffBatch(
    const std::vector<ImageData> &pixels, const std::vector<QSize> &displayTargets,
    const std::vector<CellAdjust> &adjusts, int baseIndex, uint8_t threshold, double gain,
    bool highlight, bool visualize, bool autoAlign, const mviewer::domain::Selection &roi,
    int paneCount, uint64_t generation, const TaskScheduler::TaskContext &context)
{
    DiffBatchResult result;
    result.generation = generation;
    result.baseIdx = baseIndex;
    result.diffGain = gain;
    const auto adjustFor = [&adjusts](int index) -> CellAdjust
    {
        if (index >= 0 && index < static_cast<int>(adjusts.size()))
            return adjusts[static_cast<size_t>(index)];
        return CellAdjust{};
    };
    const ImageData basePixels = CompareWorkspace::cachedAdjusted(
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
    const DiffSources sources =
        buildDiffOverlays(result, pixels, displayTargets, adjusts, baseIndex, threshold, gain,
                          highlight, visualize, autoAlign, basePixels, context);
    if (!context.isCancelled())
        computeDiffMetrics(result, sources, basePixels, threshold, roi, context);
    return result;
}

TaskScheduler::TaskHandle CompareWorkspace::startDiffBatch(
    const std::vector<ImageData> &pixels, const std::vector<QSize> &displayTargets,
    const std::vector<CellAdjust> &adjusts, int baseIndex, uint8_t threshold, double gain,
    bool highlight, bool visualize, bool autoAlign, const mviewer::domain::Selection &roi,
    int paneCount, uint64_t generation, const QPointer<CompareWorkspace> &guard)
{
    return TaskScheduler::instance().submit(
        deferredAnalysisPriority(),
        [pixels, displayTargets, adjusts, baseIndex, threshold, gain, highlight, visualize,
         autoAlign, roi, paneCount, generation, guard](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled())
                return;
            const DiffBatchResult result = CompareWorkspace::computeDiffBatch(
                pixels, displayTargets, adjusts, baseIndex, threshold, gain, highlight, visualize,
                autoAlign, roi, paneCount, generation, context);
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
    if (!m_diffOverlayVisible)
    {
        // Turning the visualization off must clear every pane now, even while
        // heavy work is deferred by a zoom/pan: otherwise the heatmap (or an
        // in-flight live preview landing late) stays until the settle.
        for (RawImageView *view : m_cellViews)
            if (view)
                view->setOverlay(QImage(), 0.0);
        if (m_compareCanvas)
            m_compareCanvas->update();
    }
    if (!m_rebuildingCells && shouldDeferHeavyCompareWork())
    {
        m_deferredDiffRefresh = true;
        if (!m_diffOverlayVisible && m_diffTask)
        {
            TaskScheduler::cancel(m_diffTask);
            m_diffTask.reset();
            ++m_diffGen;
        }
        // Keep a cheap provisional overlay responsive during zoom/pan; full
        // precision still runs in flushDeferredCompareAnalysis on settle.
        if (m_diffOverlayVisible)
            scheduleLiveDiffPreview();
        return;
    }
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
            view->setMetricBadge(QString());
        }
        presentDiffMetrics(tr("PSNR: —  SSIM: —"));
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
    const double gain = m_diffGain;
    const bool highlight = m_diffHighlight;
    const bool visualize = m_diffOverlayVisible;
    const bool autoAlign = m_autoAlignBeforeDiff;
    const mviewer::domain::Selection roi = m_lastSelection;
    const uint64_t gen = m_diffGen;
    QPointer<CompareWorkspace> guard(this);

    auto handle = startDiffBatch(pixels, displayTargets, adjusts, baseIdx, threshold, gain,
                                 highlight, visualize, autoAlign, roi, paneCount, gen, guard);
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
        // Generation already drops superseded batches; still refuse to paint a
        // heatmap after the user turned visualization off mid-flight.
        if (!m_diffOverlayVisible)
            view->setOverlay(QImage(), 0.0);
        else
            view->setOverlay(ov.overlay, ov.opacity);
        if (ov.hasMetrics && m_cellViews.size() > 2)
        {
            view->setMetricBadge(
                QString("P: %1 dB | S: %2").arg(ov.psnr, 0, 'f', 2).arg(ov.ssim, 0, 'f', 3));
            view->setToolTip(tr("对比基准: 图 %1\nPSNR: %2 dB\nSSIM: %3")
                                 .arg(r.baseIdx + 1)
                                 .arg(ov.psnr, 0, 'f', 2)
                                 .arg(ov.ssim, 0, 'f', 4));
        }
        else
        {
            view->setMetricBadge(QString());
        }
    }

    // Provisional live diff only updates the overlay; keep prior metrics until
    // the full-precision settle batch arrives.
    if (r.provisional)
    {
        update();
        return;
    }

    noteSuggestedThreshold(r.hasSuggestedThreshold, static_cast<int>(r.suggestedThreshold));

    if (m_metricLabel)
    {
        QString text;
        if (!r.metricsValid)
        {
            text =
                r.sizeMismatch ? tr("PSNR: —  SSIM: —\n(图像尺寸不一致)") : tr("PSNR: —  SSIM: —");
        }
        else
        {
            const QString psnrStr = QString::number(r.psnr, 'f', 2) + " dB";
            const QString ssimStr = QString::number(r.ssim, 'f', 4);
            text = tr("PSNR: %1    SSIM: %2\n（图 %3 vs 图 %4）")
                       .arg(psnrStr, ssimStr)
                       .arg(r.baseIdx + 1)
                       .arg(r.targetIdx + 1);
            if (r.diffGain > 1.0)
                text += tr("  [增益: %1x]").arg(r.diffGain, 0, 'f', 0);
            if (r.aligned)
                text += tr("\n对齐: (%1, %2)").arg(r.alignX).arg(r.alignY);
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
        presentDiffMetrics(text);
    }
    update();
}

void CompareWorkspace::setAutoAlignBeforeDiff(bool on)
{
    // Single in-memory owner for the preference: the compare toolbar
    // checkbox and Preferences both route here, so they never drift.
    if (m_autoAlignChk && m_autoAlignChk->isChecked() != on)
    {
        const QSignalBlocker blocker(m_autoAlignChk);
        m_autoAlignChk->setChecked(on);
    }
    if (m_autoAlignBeforeDiff == on)
        return;
    m_autoAlignBeforeDiff = on;
    refreshAllDiffOverlays();
}
