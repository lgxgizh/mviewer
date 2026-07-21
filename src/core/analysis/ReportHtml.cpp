#include "core/analysis/ReportHtml.h"

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
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        case '\'': r += "&#39;"; break;
        default: r += c; break;
        }
    }
    return r;
}

std::string tag(const std::string &name, const std::string &content)
{
    return "<" + name + ">" + content + "</" + name + ">";
}

} // namespace

std::string buildReportHtml(const ReportContext &ctx)
{
    std::ostringstream os;
    os << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>"
       << escapeHtml(ctx.title) << "</title>\n"
       << "<style>\n"
       << "body{font-family:sans-serif;max-width:960px;margin:24px auto;padding:0 16px;color:#222;}\n"
       << "h1{color:#1a73e8;border-bottom:2px solid #1a73e8;padding-bottom:8px;}\n"
       << "h2{color:#555;margin-top:32px;}\n"
       << "table{border-collapse:collapse;width:100%;margin:12px 0;}\n"
       << "th,td{border:1px solid #ddd;padding:8px;text-align:left;}\n"
       << "th{background:#1a73e8;color:#fff;}\n"
       << "img{max-width:100%;border:1px solid #ddd;margin:8px 0;}\n"
       << ".metric{color:#666;font-size:14px;}\n"
       << "</style></head><body>\n";

    os << "<h1>" << escapeHtml(ctx.title) << "</h1>\n";

    if (!ctx.imagePath.empty())
        os << tag("h2", "Image") << "<p class='metric'>" << escapeHtml(ctx.imagePath) << "</p>\n";

    if (!ctx.histogramPng.empty())
    {
        os << tag("h2", "Histogram")
           << "<img src=\"data:image/png;base64," << ctx.histogramPng << "\" alt=\"histogram\">\n";
    }

    if (ctx.hasCompare)
    {
        os << tag("h2", "Compare Result") << "<table>\n";
        os << "<tr><th>Metric</th><th>Value</th></tr>\n";
        os << "<tr><td>PSNR</td><td>" << ctx.compare.psnr << " dB</td></tr>\n";
        os << "<tr><td>SSIM</td><td>" << ctx.compare.ssim << "</td></tr>\n";
        os << "<tr><td>Diff Mean</td><td>" << ctx.compare.diffMean << "</td></tr>\n";
        os << "<tr><td>Diff Max</td><td>" << ctx.compare.diffMax << "</td></tr>\n";
        os << "</table>\n";
        if (!ctx.compareDiffPng.empty())
            os << "<img src=\"data:image/png;base64," << ctx.compareDiffPng << "\" alt=\"diff\">\n";
    }

    if (ctx.hasBatch)
    {
        os << tag("h2", "Analyzer: " + ctx.batch.analyzerId) << "<table>\n<tr><th>Filename</th>";
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

    os << "</body></html>\n";
    return os.str();
}

} // namespace mviewer::core
