#include "core/analysis/ExportReport.h"

#include "core/image/ImageAdjust.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <utility>

namespace mviewer::core
{

namespace
{

// Scan a grayscale diff map for min/mean/max (0..255).
void summarizeDiff(const ImageData &d, double &mn, double &mean, double &mx)
{
    mn = 255.0;
    mx = 0.0;
    double sum = 0.0;
    const int n = d.width * d.height;
    if (n <= 0)
    {
        mn = mean = mx = 0.0;
        return;
    }
    const uint8_t *p = d.buffer->data();
    for (int i = 0; i < n; ++i)
    {
        const double v = static_cast<double>(p[i]);
        if (v < mn)
            mn = v;
        if (v > mx)
            mx = v;
        sum += v;
    }
    mean = sum / static_cast<double>(n);
}

void fillMeanRgb(const ImageFrame &f, double &r, double &g, double &b)
{
    RGBMeanAnalyzer an;
    if (an.analyze(f))
    {
        const RGBMeanAnalyzer::Result res = an.result();
        r = res.rMean;
        g = res.gMean;
        b = res.bMean;
    }
}

void fillNoise(const ImageFrame &f, double &out)
{
    NoiseAnalyzer an;
    if (an.analyze(f))
        out = an.noiseLevel();
}

} // namespace

CompareReport buildCompareReport(const ImageFrame &a, const ImageFrame &b)
{
    CompareReport r;
    r.imageA = a.metadata().filePath;
    r.imageB = b.metadata().filePath;

    if (!a.isValid() || !b.isValid())
        return r;

    // Dual-image quality metrics: reference = A, target = B.
    PSNRAnalyzer psnr;
    psnr.setReference(a);
    if (psnr.analyze(b))
        r.psnr = psnr.psnrValue();

    SSIMAnalyzer ssim;
    ssim.setReference(a);
    if (ssim.analyze(b))
        r.ssim = ssim.ssimValue();

    // Per-image stats.
    fillMeanRgb(a, r.meanR_A, r.meanG_A, r.meanB_A);
    fillMeanRgb(b, r.meanR_B, r.meanG_B, r.meanB_B);
    fillNoise(a, r.noiseA);
    fillNoise(b, r.noiseB);

    // Diff summary.
    const ImageData diff = DifferenceEngine::differenceMap(a.pixels(), b.pixels());
    if (!diff.isNull())
        summarizeDiff(diff, r.diffMin, r.diffMean, r.diffMax);

    return r;
}

ImageData compareDiffImage(const ImageFrame &a, const ImageFrame &b)
{
    if (!a.isValid() || !b.isValid())
        return ImageData{};
    const ImageData diff = DifferenceEngine::differenceMap(a.pixels(), b.pixels());
    if (diff.isNull())
        return ImageData{};
    return DifferenceEngine::heatMap(diff);
}

bool CompareAdjustmentState::isIdentity() const
{
    return brightness == 0 && std::abs(contrast - 1.0) < 1e-6 && std::abs(gamma - 1.0) < 1e-6 &&
           std::abs(redGain - 1.0) < 1e-6 && std::abs(blueGain - 1.0) < 1e-6 && rotation == 0 &&
           !hasCrop;
}

ImageData applyCompareAdjustments(const ImageData &src,
                                  const CompareAdjustmentState &adjustment,
                                  const std::function<bool()> &cancelled)
{
    const auto shouldCancel = [&cancelled]()
    {
        return cancelled && cancelled();
    };
    if (shouldCancel())
        return {};
    if (src.isNull() || adjustment.isIdentity())
        return src;

    ImageData cur = src;
    if (adjustment.brightness != 0)
        cur = adjustBrightness(cur, adjustment.brightness);
    if (shouldCancel())
        return {};
    if (std::abs(adjustment.contrast - 1.0) >= 1e-6)
        cur = adjustContrast(cur, static_cast<float>(adjustment.contrast));
    if (shouldCancel())
        return {};
    if (std::abs(adjustment.gamma - 1.0) >= 1e-6)
        cur = adjustGamma(cur, static_cast<float>(adjustment.gamma));
    if (shouldCancel())
        return {};
    if (std::abs(adjustment.redGain - 1.0) >= 1e-6 ||
        std::abs(adjustment.blueGain - 1.0) >= 1e-6)
    {
        cur = adjustWhiteBalance(cur, static_cast<float>(adjustment.redGain),
                                 static_cast<float>(adjustment.blueGain));
    }
    if (shouldCancel())
        return {};

    if (adjustment.hasCrop && adjustment.cropW > 0 && adjustment.cropH > 0)
    {
        const mviewer::domain::Selection selection{adjustment.cropX, adjustment.cropY,
                                                   adjustment.cropW, adjustment.cropH};
        cur = cropRegion(cur, selection);
    }
    if (shouldCancel())
        return {};

    if (adjustment.rotation != 0)
    {
        int rotation = adjustment.rotation % 360;
        if (rotation < 0)
            rotation += 360;
        while (rotation > 0)
        {
            if (shouldCancel())
                return {};
            cur = rotate90CW(cur);
            rotation -= 90;
        }
    }
    if (shouldCancel())
        return {};
    return cur;
}

namespace
{

bool reportCancelled(const ReportBuildCallbacks &callbacks)
{
    return callbacks.cancelled && callbacks.cancelled();
}

void reportProgress(const ReportBuildCallbacks &callbacks, int value)
{
    if (!callbacks.progress)
        return;
    callbacks.progress(std::clamp(value, 0, 100));
}

} // namespace

CompareReportBundle buildCompareReportBundle(
    const std::vector<ImageFrame> &adjustedImages, int referenceIndex, uint8_t threshold,
    const mviewer::domain::Selection &roi,
    const std::vector<CompareAdjustmentState> &adjustments,
    const ReportBuildCallbacks &callbacks)
{
    CompareReportBundle bundle;
    bundle.referenceIndex = referenceIndex;
    bundle.threshold = threshold;
    bundle.roi = roi;
    reportProgress(callbacks, 0);
    bundle.images.reserve(adjustedImages.size());
    for (const auto &image : adjustedImages)
        bundle.images.push_back(image.metadata().filePath);

    bundle.adjustments.resize(adjustedImages.size());
    const size_t adjustmentCount = std::min(adjustments.size(), bundle.adjustments.size());
    for (size_t i = 0; i < adjustmentCount; ++i)
        bundle.adjustments[i] = adjustments[i];

    const bool validReference = referenceIndex >= 0 &&
                                referenceIndex < static_cast<int>(adjustedImages.size());
    if (validReference)
    {
        bundle.targets.reserve(adjustedImages.size() - 1);
        const size_t targetCount = adjustedImages.size() - 1;
        size_t completedTargets = 0;
        for (size_t i = 0; i < adjustedImages.size(); ++i)
        {
            if (static_cast<int>(i) == referenceIndex)
                continue;
            if (reportCancelled(callbacks))
                return bundle;

            const ImageFrame &reference = adjustedImages[static_cast<size_t>(referenceIndex)];
            const ImageFrame &target = adjustedImages[i];
            CompareReportPair pair;
            pair.index = static_cast<int>(i);
            pair.path = target.metadata().filePath;
            pair.referenceIndex = referenceIndex;
            pair.imageA = reference.metadata().filePath;

            const bool sameSize = reference.isValid() && target.isValid() &&
                                  reference.width() == target.width() &&
                                  reference.height() == target.height();
            if (sameSize)
            {
                const ImageData rawDiff =
                    DifferenceEngine::differenceMap(reference.pixels(), target.pixels());
                if (reportCancelled(callbacks))
                    return bundle;
                if (!rawDiff.isNull())
                {
                    pair.comparable = true;
                    PSNRAnalyzer psnr;
                    psnr.setReference(reference);
                    if (psnr.analyze(target))
                        pair.psnr = psnr.psnrValue();
                    if (reportCancelled(callbacks))
                        return bundle;

                    SSIMAnalyzer ssim;
                    ssim.setReference(reference);
                    if (ssim.analyze(target))
                        pair.ssim = ssim.ssimValue();
                    if (reportCancelled(callbacks))
                        return bundle;

                    pair.fullDiffStats = DifferenceEngine::computeStats(rawDiff, threshold);
                    if (reportCancelled(callbacks))
                        return bundle;
                    if (!roi.isEmpty())
                    {
                        const DifferenceEngine::DiffStats roiStats = DifferenceEngine::computeStats(
                            rawDiff, threshold, roi.x, roi.y, roi.width, roi.height);
                        if (roiStats.totalPixels > 0)
                            pair.roiDiffStats = roiStats;
                    }
                    if (reportCancelled(callbacks))
                        return bundle;
                    const ImageData thresholded =
                        DifferenceEngine::applyThreshold(rawDiff, threshold);
                    if (reportCancelled(callbacks))
                        return bundle;
                    pair.diffHeatmap = DifferenceEngine::heatMap(thresholded);
                    if (reportCancelled(callbacks))
                        return bundle;
                }
            }
            bundle.targets.push_back(std::move(pair));
            ++completedTargets;
            const int targetProgress =
                targetCount == 0 ? 100 : static_cast<int>(completedTargets * 100 / targetCount);
            reportProgress(callbacks, targetProgress);
        }
    }
    else
    {
        bundle.targets.reserve(adjustedImages.size());
        const size_t targetCount = adjustedImages.size();
        for (size_t i = 0; i < adjustedImages.size(); ++i)
        {
            if (reportCancelled(callbacks))
                return bundle;
            CompareReportPair pair;
            pair.index = static_cast<int>(i);
            pair.path = adjustedImages[i].metadata().filePath;
            pair.referenceIndex = referenceIndex;
            bundle.targets.push_back(std::move(pair));
            reportProgress(callbacks, targetCount == 0
                                             ? 100
                                             : static_cast<int>((i + 1) * 100 / targetCount));
        }
    }
    reportProgress(callbacks, 100);
    return bundle;
}

CompareReportBundle buildCompareReportBundle(const CompareReportInput &input,
                                             const ReportBuildCallbacks &callbacks)
{
    reportProgress(callbacks, 0);
    std::vector<ImageFrame> adjustedImages;
    adjustedImages.reserve(input.images.size());
    std::vector<CompareAdjustmentState> adjustments;
    adjustments.reserve(input.images.size());

    for (size_t i = 0; i < input.images.size(); ++i)
    {
        if (reportCancelled(callbacks))
            return {};

        const CompareReportSource &source = input.images[i];
        const CompareAdjustmentState &adjustment = source.adjustment;
        const ImageData pixels = applyCompareAdjustments(source.pixels, adjustment,
                                                          callbacks.cancelled);
        if (reportCancelled(callbacks))
            return {};

        mviewer::domain::ImageMetadata metadata = source.metadata;
        metadata.width = pixels.width;
        metadata.height = pixels.height;
        adjustedImages.emplace_back(metadata, pixels);
        adjustments.push_back(adjustment);
        reportProgress(callbacks, input.images.empty()
                                         ? 35
                                         : static_cast<int>((i + 1) * 35 / input.images.size()));
    }

    if (reportCancelled(callbacks))
        return {};

    // Keep the core API independent from TaskScheduler while mapping the
    // bundle stage into the final 35..100% report-worker progress range.
    ReportBuildCallbacks metricsCallbacks = callbacks;
    const std::function<void(int)> progress = callbacks.progress;
    metricsCallbacks.progress = [progress](int value)
    {
        if (progress)
            progress(35 + value * 65 / 100);
    };
    CompareReportBundle bundle = buildCompareReportBundle(
        adjustedImages, input.referenceIndex, input.threshold, input.roi, adjustments,
        metricsCallbacks);
    // The compatibility builder intentionally preserves its historical
    // partial-result behavior. A source snapshot, however, is the worker's
    // success boundary and must never publish a half-built bundle.
    if (reportCancelled(callbacks))
        return {};
    return bundle;
}

// ─── M13.4 batch analyzer export ──────────────────────────────────────────
AnalysisBatchReport buildBatchReport(const std::string &analyzerId,
                                     const std::vector<mviewer::analyzer::AnalyzerResult> &results)
{
    AnalysisBatchReport rep;
    rep.analyzerId = analyzerId;

    // Union of metric keys in first-seen order for a stable column layout.
    std::unordered_set<std::string> seen;
    for (const auto &r : results)
        for (const auto &[key, val] : r.metrics)
            if (seen.insert(key).second)
                rep.columns.push_back(key);

    rep.filenames.reserve(results.size());
    rep.rows.reserve(results.size());
    for (const auto &r : results)
    {
        rep.filenames.push_back(r.filename);
        rep.rows.push_back(r.metrics);
    }
    return rep;
}

} // namespace mviewer::core
