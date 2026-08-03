#include "core/analysis/ExportReport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace mviewer::core
{

namespace
{

std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0x0f];
                out += hex[c & 0x0f];
            }
            else
            {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

std::string csvEscape(const std::string &s)
{
    const bool needQuote = s.find(',') != std::string::npos || s.find('"') != std::string::npos ||
                           s.find('\n') != std::string::npos || s.find('\r') != std::string::npos;
    if (!needQuote)
        return s;

    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

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

namespace
{

void writeJsonNumber(std::ostringstream &os, double value)
{
    if (std::isfinite(value))
        os << value;
    else
        os << "null";
}

std::string csvNumber(double value)
{
    if (!std::isfinite(value))
        return {};
    std::ostringstream os;
    os << value;
    return os.str();
}

void writeDiffStatsJson(std::ostringstream &os, const DifferenceEngine::DiffStats &stats)
{
    os << "{\"totalPixels\": " << stats.totalPixels << ", \"diffPixels\": " << stats.diffPixels
       << ", \"diffRatio\": ";
    writeJsonNumber(os, stats.diffRatio);
    os << ", \"meanDiff\": ";
    writeJsonNumber(os, stats.meanDiff);
    os << ", \"maxDiff\": " << stats.maxDiff << "}";
}

void writeAdjustmentJson(std::ostringstream &os, const CompareAdjustmentState &adjustment)
{
    os << "{\"brightness\": " << adjustment.brightness << ", \"contrast\": ";
    writeJsonNumber(os, adjustment.contrast);
    os << ", \"gamma\": ";
    writeJsonNumber(os, adjustment.gamma);
    os << ", \"redGain\": ";
    writeJsonNumber(os, adjustment.redGain);
    os << ", \"blueGain\": ";
    writeJsonNumber(os, adjustment.blueGain);
    os << ", \"rotation\": " << adjustment.rotation << ", \"hasCrop\": "
       << (adjustment.hasCrop ? "true" : "false")
       << ", \"crop\": {\"x\": " << adjustment.cropX << ", \"y\": " << adjustment.cropY
       << ", \"width\": " << adjustment.cropW << ", \"height\": " << adjustment.cropH << "}}";
}

void appendDiffStatsCsv(std::vector<std::string> &fields,
                        const DifferenceEngine::DiffStats *stats)
{
    if (stats == nullptr)
    {
        fields.insert(fields.end(), 5, std::string{});
        return;
    }
    fields.push_back(std::to_string(stats->totalPixels));
    fields.push_back(std::to_string(stats->diffPixels));
    fields.push_back(csvNumber(stats->diffRatio));
    fields.push_back(csvNumber(stats->meanDiff));
    fields.push_back(std::to_string(stats->maxDiff));
}

} // namespace

CompareReportBundle buildCompareReportBundle(
    const std::vector<ImageFrame> &adjustedImages, int referenceIndex, uint8_t threshold,
    const mviewer::domain::Selection &roi,
    const std::vector<CompareAdjustmentState> &adjustments)
{
    CompareReportBundle bundle;
    bundle.referenceIndex = referenceIndex;
    bundle.threshold = threshold;
    bundle.roi = roi;
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
        for (size_t i = 0; i < adjustedImages.size(); ++i)
        {
            if (static_cast<int>(i) == referenceIndex)
                continue;

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
                if (!rawDiff.isNull())
                {
                    pair.comparable = true;
                    PSNRAnalyzer psnr;
                    psnr.setReference(reference);
                    if (psnr.analyze(target))
                        pair.psnr = psnr.psnrValue();

                    SSIMAnalyzer ssim;
                    ssim.setReference(reference);
                    if (ssim.analyze(target))
                        pair.ssim = ssim.ssimValue();

                    pair.fullDiffStats = DifferenceEngine::computeStats(rawDiff, threshold);
                    if (!roi.isEmpty())
                    {
                        const DifferenceEngine::DiffStats roiStats = DifferenceEngine::computeStats(
                            rawDiff, threshold, roi.x, roi.y, roi.width, roi.height);
                        if (roiStats.totalPixels > 0)
                            pair.roiDiffStats = roiStats;
                    }
                    const ImageData thresholded =
                        DifferenceEngine::applyThreshold(rawDiff, threshold);
                    pair.diffHeatmap = DifferenceEngine::heatMap(thresholded);
                }
            }
            bundle.targets.push_back(std::move(pair));
        }
    }
    else
    {
        bundle.targets.reserve(adjustedImages.size());
        for (size_t i = 0; i < adjustedImages.size(); ++i)
        {
            CompareReportPair pair;
            pair.index = static_cast<int>(i);
            pair.path = adjustedImages[i].metadata().filePath;
            pair.referenceIndex = referenceIndex;
            bundle.targets.push_back(std::move(pair));
        }
    }
    return bundle;
}

std::string CompareReportBundle::toJson() const
{
    std::ostringstream os;
    os << "{\n  \"images\": [";
    for (size_t i = 0; i < images.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        os << "\"" << jsonEscape(images[i]) << "\"";
    }
    os << "],\n  \"referenceIndex\": " << referenceIndex << ",\n  \"threshold\": "
       << static_cast<unsigned int>(threshold) << ",\n  \"roi\": {\"x\": " << roi.x
       << ", \"y\": " << roi.y << ", \"width\": " << roi.width << ", \"height\": "
       << roi.height << "},\n  \"adjustments\": [";
    for (size_t i = 0; i < adjustments.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        writeAdjustmentJson(os, adjustments[i]);
    }
    os << "],\n  \"targets\": [";
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const CompareReportPair &pair = targets[i];
        if (i != 0)
            os << ",";
        os << "\n    {\"index\": " << pair.index << ", \"path\": \"" << jsonEscape(pair.path)
           << "\", \"referenceIndex\": " << pair.referenceIndex << ", \"imageA\": \""
           << jsonEscape(pair.imageA) << "\", \"comparable\": "
           << (pair.comparable ? "true" : "false") << ", \"psnr_dB\": ";
        if (pair.comparable)
            writeJsonNumber(os, pair.psnr);
        else
            os << "null";
        os << ", \"ssim\": ";
        if (pair.comparable)
            writeJsonNumber(os, pair.ssim);
        else
            os << "null";
        os << ", \"fullDiffStats\": ";
        if (pair.comparable)
            writeDiffStatsJson(os, pair.fullDiffStats);
        else
            os << "null";
        os << ", \"roiDiffStats\": ";
        if (pair.comparable && pair.roiDiffStats.has_value())
            writeDiffStatsJson(os, *pair.roiDiffStats);
        else
            os << "null";
        os << "}";
    }
    if (!targets.empty())
        os << "\n  ";
    os << "]\n}\n";
    return os.str();
}

std::string CompareReportBundle::toCsv() const
{
    std::ostringstream os;
    os << "referenceIndex,imageA,index,path,comparable,psnr_dB,ssim,"
          "full_totalPixels,full_diffPixels,full_diffRatio,full_meanDiff,full_maxDiff,"
          "roi_totalPixels,roi_diffPixels,roi_diffRatio,roi_meanDiff,roi_maxDiff\n";
    for (const auto &pair : targets)
    {
        std::vector<std::string> fields;
        fields.reserve(17);
        fields.push_back(std::to_string(pair.referenceIndex));
        fields.push_back(pair.imageA);
        fields.push_back(std::to_string(pair.index));
        fields.push_back(pair.path);
        fields.push_back(pair.comparable ? "true" : "false");
        if (pair.comparable)
        {
            fields.push_back(csvNumber(pair.psnr));
            fields.push_back(csvNumber(pair.ssim));
            appendDiffStatsCsv(fields, &pair.fullDiffStats);
            appendDiffStatsCsv(
                fields, pair.roiDiffStats.has_value() ? &*pair.roiDiffStats : nullptr);
        }
        else
        {
            fields.insert(fields.end(), 12, std::string{});
        }
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (i != 0)
                os << ",";
            os << csvEscape(fields[i]);
        }
        os << "\n";
    }
    return os.str();
}

std::string CompareReport::toJson() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"imageA\": \"" << imageA << "\",\n";
    os << "  \"imageB\": \"" << imageB << "\",\n";
    os << "  \"psnr_dB\": " << psnr << ",\n";
    os << "  \"ssim\": " << ssim << ",\n";
    os << "  \"meanRGB_A\": [" << meanR_A << ", " << meanG_A << ", " << meanB_A << "],\n";
    os << "  \"meanRGB_B\": [" << meanR_B << ", " << meanG_B << ", " << meanB_B << "],\n";
    os << "  \"noise_A\": " << noiseA << ",\n";
    os << "  \"noise_B\": " << noiseB << ",\n";
    os << "  \"diff\": { \"min\": " << diffMin << ", \"mean\": " << diffMean
       << ", \"max\": " << diffMax << " }\n";
    os << "}\n";
    return os.str();
}

std::string CompareReport::toCsv() const
{
    std::ostringstream os;
    os << "imageA,imageB,psnr_dB,ssim,meanR_A,meanG_A,meanB_A,"
          "meanR_B,meanG_B,meanB_B,noise_A,noise_B,diff_min,diff_mean,diff_max\n";
    os << imageA << "," << imageB << "," << psnr << "," << ssim << "," << meanR_A << "," << meanG_A
       << "," << meanB_A << "," << meanR_B << "," << meanG_B << "," << meanB_B << "," << noiseA
       << "," << noiseB << "," << diffMin << "," << diffMean << "," << diffMax << "\n";
    return os.str();
}

// ─── M13.4 batch analyzer export ──────────────────────────────────────────
namespace
{
} // namespace

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

std::string AnalysisBatchReport::toCsv() const
{
    std::ostringstream os;
    os << "filename";
    for (const auto &c : columns)
        os << "," << csvEscape(c);
    os << "\n";

    for (size_t i = 0; i < filenames.size(); ++i)
    {
        os << csvEscape(filenames[i]);
        const auto &row = rows[i];
        for (const auto &c : columns)
        {
            os << ",";
            auto it = row.find(c);
            if (it != row.end())
                os << it->second;
        }
        os << "\n";
    }
    return os.str();
}

std::string AnalysisBatchReport::toJson() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"analyzer\": \"" << jsonEscape(analyzerId) << "\",\n";
    os << "  \"columns\": [";
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i)
            os << ", ";
        os << "\"" << jsonEscape(columns[i]) << "\"";
    }
    os << "],\n";
    os << "  \"rows\": [";
    for (size_t i = 0; i < filenames.size(); ++i)
    {
        if (i)
            os << ",";
        os << "\n    { \"filename\": \"" << jsonEscape(filenames[i]) << "\"";
        const auto &row = rows[i];
        for (const auto &c : columns)
        {
            auto it = row.find(c);
            if (it != row.end())
                os << ", \"" << jsonEscape(c) << "\": " << it->second;
        }
        os << " }";
    }
    if (!filenames.empty())
        os << "\n  ";
    os << "]\n";
    os << "}\n";
    return os.str();
}

} // namespace mviewer::core
