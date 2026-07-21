#pragma once

#include "core/analysis/ExportReport.h"
#include "core/image/ImageFrame.h"

#include <string>
#include <vector>

namespace mviewer::core
{

// HTML report for algorithm evaluation. Embeds image thumbnails as base64
// and renders analyzer/compare results in a structured layout.
struct ReportContext
{
    std::string title = "MViewer Analysis Report";
    std::string imagePath;
    std::string histogramPng;   // base64-encoded PNG
    std::string compareDiffPng; // base64-encoded PNG (diff image)
    CompareReport compare;
    AnalysisBatchReport batch;
    bool hasCompare = false;
    bool hasBatch = false;
};

// Build a self-contained HTML report from the given context.
std::string buildReportHtml(const ReportContext &ctx);

} // namespace mviewer::core
