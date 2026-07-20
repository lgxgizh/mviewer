#include "core/analysis/ExportReport.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_set>

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

// Minimal JSON string escaper (quotes, backslash, control chars). Filenames
// may contain backslashes on Windows, so this matters for valid JSON.
std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
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
            out += c;
            break;
        }
    }
    return out;
}

// CSV field escaper: quote if the value contains a comma, quote or newline.
std::string csvEscape(const std::string &s)
{
    const bool needQuote =
        s.find(',') != std::string::npos || s.find('"') != std::string::npos ||
        s.find('\n') != std::string::npos || s.find('\r') != std::string::npos;
    if (!needQuote)
        return s;
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

} // namespace

AnalysisBatchReport
buildBatchReport(const std::string &analyzerId,
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
