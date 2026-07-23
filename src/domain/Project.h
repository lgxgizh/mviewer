#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Workspace.h"

// M15 (Project): the unifying "evaluation project" concept that promotes
// MViewer from an image viewer into an image-algorithm evaluation platform.
// A .mvproj file captures the entire evaluation environment — datasets, the
// compare session (layout / zoom / ROI / sync), per-image analysis, the
// analyzer pipeline, and export / review / benchmark configuration — so
// opening one file restores the full workspace an ISP / Camera / CV engineer
// was working in.
namespace mviewer
{
namespace domain
{

struct Project
{
    std::string name;
    std::string filePath; // absolute path of the .mvproj file
    std::string createdIso;
    std::string modifiedIso;
    std::string appVersion;

    // The embedded workspace: datasets + compared images + compare-session
    // snapshot + per-image ROI/analysis. This is the part that already restores
    // the browsing + compare environment on reload.
    Workspace workspace;

    // M15 Project facets layered on top of the workspace:
    std::vector<std::string> datasetRoots;     // extra dataset directories
    std::vector<std::string> analyzerPipeline; // analyzer ids (AnalyzerRegistry order)
    std::string analyzerPipelineJson;          // detailed pipeline config (forward-compat)
    std::string exportConfigJson;              // export settings (forward-compat)
    std::string benchmarkBaselineJson;         // benchmark baseline (forward-compat)
    std::string reviewNotes;                   // Review Report notes
};

} // namespace domain
} // namespace mviewer
