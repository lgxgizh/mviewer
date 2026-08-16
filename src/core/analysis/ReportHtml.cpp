#include "core/analysis/ReportHtml.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mviewer::core
{

namespace
{

std::string escapeHtml(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        case '\'':
            r += "&#39;";
            break;
        default:
            r += c;
            break;
        }
    }
    return r;
}

std::string tag(const std::string &name, const std::string &content)
{
    return "<" + name + ">" + content + "</" + name + ">";
}

std::string formatNumber(double value)
{
    if (!std::isfinite(value))
        return "Unavailable";
    std::ostringstream os;
    os << std::setprecision(8) << value;
    return os.str();
}

std::string formatDiffStats(const DifferenceEngine::DiffStats &stats)
{
    std::ostringstream os;
    os << "diff pixels=" << stats.diffPixels << "/" << stats.totalPixels
       << "; ratio=" << formatNumber(stats.diffRatio)
       << "; mean=" << formatNumber(stats.meanDiff) << "; max=" << stats.maxDiff;
    return os.str();
}

std::string formatAdjustment(const CompareAdjustmentState &adjustment)
{
    std::ostringstream os;
    os << "brightness=" << adjustment.brightness << "; contrast="
       << formatNumber(adjustment.contrast) << "; gamma=" << formatNumber(adjustment.gamma)
       << "; redGain=" << formatNumber(adjustment.redGain)
       << "; blueGain=" << formatNumber(adjustment.blueGain)
       << "; rotation=" << adjustment.rotation << "°; crop=";
    if (adjustment.hasCrop)
        os << "(" << adjustment.cropX << "," << adjustment.cropY << "," << adjustment.cropW
           << "x" << adjustment.cropH << ")";
    else
        os << "none";
    return os.str();
}

void appendImageHtml(std::ostringstream &os, const ReportContext &ctx)
{
    if (!ctx.imagePath.empty())
        os << tag("h2", "Image") << "<p class='metric'>" << escapeHtml(ctx.imagePath) << "</p>\n";

    if (!ctx.histogramPng.empty())
    {
        os << tag("h2", "Histogram") << "<img src=\"data:image/png;base64," << ctx.histogramPng
           << "\" alt=\"histogram\">\n";
    }
}

void appendAnalysisHtml(std::ostringstream &os, const ReportContext &ctx)
{
    if (!ctx.hasAnalysis)
        return;

    os << tag("h2", "Analysis") << "\n<table>\n"
          "<tr><th>Analyzer ID</th><th>Result</th></tr>\n<tr><td>"
       << escapeHtml(ctx.analysis.analyzerId) << "</td><td><pre>"
       << escapeHtml(ctx.analysis.resultText) << "</pre></td></tr>\n</table>\n";
}

void appendCompareHtml(std::ostringstream &os, const ReportContext &ctx)
{
    if (ctx.hasCompareBundle)
    {
        os << tag("h2", "Compare Bundle") << "\n";
        const auto &bundle = ctx.compareBundle;
        const bool validReference = bundle.referenceIndex >= 0 &&
                                    bundle.referenceIndex < static_cast<int>(bundle.images.size());
        if (validReference)
        {
            os << "<p><strong>Locked reference</strong>: image #" << (bundle.referenceIndex + 1)
               << " — " << escapeHtml(bundle.images[static_cast<size_t>(bundle.referenceIndex)])
               << "</p>\n";
        }
        else
        {
            os << "<p><strong>Locked reference</strong>: unavailable</p>\n";
        }
        os << "<p><strong>Threshold</strong>: " << static_cast<unsigned int>(bundle.threshold)
           << "</p>\n";
        os << "<p><strong>ROI</strong>: x=" << bundle.roi.x << ", y=" << bundle.roi.y
           << ", width=" << bundle.roi.width << ", height=" << bundle.roi.height << "</p>\n";

        os << "<h3>Image adjustment provenance</h3>\n<table>\n"
              "<tr><th>Image</th><th>Role</th><th>Path</th><th>Adjustment</th></tr>\n";
        for (size_t i = 0; i < bundle.images.size(); ++i)
        {
            const CompareAdjustmentState adjustment =
                i < bundle.adjustments.size() ? bundle.adjustments[i] : CompareAdjustmentState{};
            os << "<tr><td>#" << (i + 1) << "</td><td>"
               << (static_cast<int>(i) == bundle.referenceIndex ? "Locked reference" : "Target")
               << "</td><td>" << escapeHtml(bundle.images[i]) << "</td><td>"
               << escapeHtml(formatAdjustment(adjustment)) << "</td></tr>\n";
        }
        os << "</table>\n";

        os << "<h3>Target results</h3>\n<table>\n"
              "<tr><th>Target</th><th>Status</th><th>PSNR</th><th>SSIM</th>"
              "<th>Full diff stats</th><th>ROI diff stats</th><th>Diff</th></tr>\n";
        for (size_t i = 0; i < bundle.targets.size(); ++i)
        {
            const CompareReportPair &pair = bundle.targets[i];
            os << "<tr><td>image #" << (pair.index + 1) << "<br>"
               << escapeHtml(pair.path) << "</td><td>";
            if (pair.comparable)
            {
                os << "Comparable</td><td>" << formatNumber(pair.psnr) << " dB</td><td>"
                   << formatNumber(pair.ssim) << "</td><td>"
                   << escapeHtml(formatDiffStats(pair.fullDiffStats)) << "</td><td>";
                if (pair.roiDiffStats.has_value())
                    os << escapeHtml(formatDiffStats(*pair.roiDiffStats));
                else
                    os << "No ROI pixels";
                os << "</td>";
            }
            else
            {
                os << "<strong>Not comparable</strong></td><td>N/A</td><td>N/A</td>"
                      "<td>N/A — not comparable</td><td>N/A — not comparable</td>";
            }

            os << "<td>";
            if (i < ctx.compareDiffPngs.size() && !ctx.compareDiffPngs[i].empty())
            {
                os << "<img src=\"data:image/png;base64," << ctx.compareDiffPngs[i]
                   << "\" alt=\"diff for " << escapeHtml(pair.path) << "\">";
            }
            else
            {
                os << "Unavailable";
            }
            os << "</td></tr>\n";
        }
        os << "</table>\n";
    }
    else if (ctx.hasCompare)
    {
        os << tag("h2", "Compare Result") << "<table>\n";
        os << "<tr><th>Metric</th><th>Value</th></tr>\n";
        os << "<tr><td>Image A</td><td>" << escapeHtml(ctx.compare.imageA) << "</td></tr>\n";
        os << "<tr><td>Image B</td><td>" << escapeHtml(ctx.compare.imageB) << "</td></tr>\n";
        os << "<tr><td>PSNR</td><td>" << formatNumber(ctx.compare.psnr) << " dB</td></tr>\n";
        os << "<tr><td>SSIM</td><td>" << formatNumber(ctx.compare.ssim) << "</td></tr>\n";
        os << "<tr><td>Diff Mean</td><td>" << formatNumber(ctx.compare.diffMean) << "</td></tr>\n";
        os << "<tr><td>Diff Max</td><td>" << formatNumber(ctx.compare.diffMax) << "</td></tr>\n";
        os << "</table>\n";
        if (!ctx.compareDiffPng.empty())
            os << "<img src=\"data:image/png;base64," << ctx.compareDiffPng << "\" alt=\"diff\">\n";
    }
}

void appendBatchHtml(std::ostringstream &os, const ReportContext &ctx)
{
    if (ctx.hasBatch)
    {
        os << tag("h2", "Analyzer: " + escapeHtml(ctx.batch.analyzerId))
           << "<table>\n<tr><th>Filename</th>";
        for (const auto &c : ctx.batch.columns)
            os << "<th>" << escapeHtml(c) << "</th>";
        os << "</tr>\n";
        for (size_t i = 0; i < ctx.batch.filenames.size(); ++i)
        {
            os << "<tr><td>" << escapeHtml(ctx.batch.filenames[i]) << "</td>";
            for (const auto &c : ctx.batch.columns)
            {
                os << "<td>";
                auto it = ctx.batch.rows[i].find(c);
                if (it != ctx.batch.rows[i].end())
                    os << it->second;
                os << "</td>";
            }
            os << "</tr>\n";
        }
        os << "</table>\n";
    }
}
} // namespace

std::string buildReportHtml(const ReportContext &ctx)
{
    std::ostringstream os;
    os << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" << escapeHtml(ctx.title)
       << "</title>\n"
       << "<style>\n"
       << "body{font-family:sans-serif;max-width:960px;margin:24px auto;padding:0 "
          "16px;color:#222;}\n"
       << "h1{color:#1a73e8;border-bottom:2px solid #1a73e8;padding-bottom:8px;}\n"
       << "h2{color:#555;margin-top:32px;}\n"
       << "table{border-collapse:collapse;width:100%;margin:12px 0;}\n"
       << "th,td{border:1px solid #ddd;padding:8px;text-align:left;}\n"
       << "th{background:#1a73e8;color:#fff;}\n"
       << "img{max-width:100%;border:1px solid #ddd;margin:8px 0;}\n"
       << "pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:0;}\n"
       << ".metric{color:#666;font-size:14px;}\n"
       << "</style></head><body>\n";
    os << "<h1>" << escapeHtml(ctx.title) << "</h1>\n";
    appendImageHtml(os, ctx);
    appendAnalysisHtml(os, ctx);
    if (ctx.hasCompareBundle || ctx.hasCompare)
        appendCompareHtml(os, ctx);
    appendBatchHtml(os, ctx);
    os << "</body></html>\n";
    return os.str();
}
} // namespace mviewer::core
