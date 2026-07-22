#pragma once

#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFrame.h"
#include "domain/Selection.h"

#include <string>
#include <unordered_map>
#include <vector>

// M15 P0#3 — Analyzer Pipeline.
//
// The orchestration layer that turns a decoded ImageFrame into a set of
// analyzer results. It sits BETWEEN the UI (AnalysisPanel) and the
// AnalyzerRegistry so that:
//   * AnalysisPanel depends on the pipeline, not on AnalyzerRegistry directly
//     (removes the MainWindow/Panel -> AnalyzerRegistry coupling).
//   * Adding a new analyzer only requires registering it in the AnalyzerFactory;
//     neither MainWindow nor AnalysisPanel needs to change (acceptance: "新增
//     Analyzer 时 MainWindow 0 修改").
//
// Header is Qt-free (core/domain law). The pipeline is a thin, UI-agnostic
// facade over AnalyzerRegistry; it does not own analyzer state, only drives
// the registry's lifecycle-free "run every registered analyzer" convenience.
class AnalyzerPipeline
{
  public:
    AnalyzerPipeline() = default;

    // Run every registered analyzer over the full frame and return each one's
    // human-readable result, keyed by analyzer id. Analyzers that fail are
    // omitted, matching AnalyzerRegistry::runAnalyzer() semantics.
    std::unordered_map<std::string, std::string>
    run(const ImageFrame &frame) const
    {
        return AnalyzerRegistry::instance().runAnalyzer(frame);
    }

    // Run a single analyzer (by id) over a rectangular region of the frame.
    // Returns the human-readable result, or empty if the id is unknown or the
    // analysis fails.
    std::string runRegion(const ImageFrame &frame,
                          const mviewer::domain::Selection &region,
                          const std::string &id) const
    {
        auto a = AnalyzerRegistry::instance().create(id);
        if (!a)
            return {};
        if (!a->analyzeRegion(frame, region))
            return {};
        return a->resultText();
    }

    // Ids of every analyzer currently registered in the pipeline.
    std::vector<std::string> analyzerIds() const
    {
        return AnalyzerRegistry::instance().availableAnalyzers();
    }

    // Create a fresh instance of the analyzer identified by `id`, or nullptr if
    // the id is unknown. The caller owns the returned instance (unique_ptr with
    // the analyzer-specific deleter for safe cross-module destruction).
    std::unique_ptr<Analyzer, AnalyzerDeleter> create(const std::string &id) const
    {
        return AnalyzerRegistry::instance().create(id);
    }

    // Access the underlying registry. Provided so UI code that needs registry
    // metadata (e.g. AnalyzerInfo for the plugin combo) can obtain it through the
    // pipeline rather than calling AnalyzerRegistry::instance() itself.
    AnalyzerRegistry &registry() const
    {
        return AnalyzerRegistry::instance();
    }
};
