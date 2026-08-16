#pragma once

// Shared worker payload for the async Analysis panel path (M46 split of
// analysispanel_analysis.cpp). Both the job-scheduling TU
// (analysispanel_analysis_worker.cpp) and the result-consuming TU
// (analysispanel_analysis.cpp) need the complete types; the panel header only
// forward-declares them.
//
// Everything in the worker payload is snapshot BY VALUE on the UI thread; the
// worker only touches its captures (plus the TaskContext) — never the panel,
// model, widgets, registry, pipeline, or any QObject.

#include "core/analysis/AnalysisEngine.h" // ImageStats
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFrame.h"
#include "domain/Histogram.h"
#include "domain/Selection.h"

#include <QImage>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

// Everything the Analysis worker needs, snapshot BY VALUE on the UI thread.
// The worker only touches these captures (plus the TaskContext) — never the
// panel, model, widgets, registry, pipeline, or any QObject.
struct AnalysisInput
{
    std::shared_ptr<ImageFrame> frame;   // holds the pixel buffer alive
    std::string path;                    // publish path (setImage path wins, else frame metadata)
    bool hasROI = false;
    mviewer::domain::Selection roi;
    std::string id;                      // selected analyzer id (empty = none)
    uint64_t generation = 0;
    std::shared_ptr<Analyzer> analyzer;  // created on the UI thread, worker-owned
    bool creationFailed = false;         // pipeline/registry create() threw (M24 C#7)
    bool materialize = false;            // full frame materialization (vs analyzer-only rerun)
    QImage image; // already materialized RGB32, captured only for the legacy
                  // ROI fallback (analyzer-only rerun; implicit sharing)
};

// ── Worker output, marshalled to the UI thread via qApp ───────────────────
// Copyable by design so the queued delivery lambda can capture it by value.
// (Global scope: AnalysisPanel::applyAnalysisResult is declared with a
// forward-declared `struct AnalysisResult;` in the header.)
struct AnalysisResult
{
    uint64_t generation = 0;
    std::shared_ptr<ImageFrame> frame;   // identity check on the UI thread
    QImage image;                        // RGB32 (null = nothing to present)
    std::string path;
    std::string id;
    ImageStats stats;
    ImageStats roiStats;   // legacy ROI-fallback stats, kept separate from stats
    bool hasRoiStats = false;
    double noise = 0.0;
    bool noiseValid = false;

    bool materialize = false;            // mirrors the request mode
    bool isBuiltinCompare = false;
    bool analyzerOk = false;
    bool analyzerFailed = false;         // analyzer threw (M24 C#7 degrade)
    bool roiFallback = false;            // no analyzer result + ROI set (legacy fallback)
    bool noResult = false;               // no analyzer result and no ROI — explicit note
    std::string analyzerName;
    std::string resultText;
    std::unordered_map<std::string, double> metrics;
    mviewer::domain::Histogram histogram; // populated for HistogramAnalyzer
    bool hasHistogram = false;
    std::string plainResult;             // plain ROI summary to publish
};
