#pragma once

#include "core/analysis/ExportReport.h"

namespace mviewer::core
{

// Build a self-contained HTML report from the given context.
std::string buildReportHtml(const ReportContext &ctx);

} // namespace mviewer::core
