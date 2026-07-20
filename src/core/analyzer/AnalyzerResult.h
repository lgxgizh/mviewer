#pragma once

// Structured, per-image result of running a single analyzer over a batch of
// frames. Qt-free (core/domain law): scalar metrics live in a string->double
// map so any analyzer contributes without bespoke UI/serialization code, and
// `detail` carries the human-readable resultText() for reference.
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ImageFrame lives in the global namespace (see core/image/ImageFrame.h).
class ImageFrame;

namespace mviewer::analyzer
{

// One row of a batch analysis: the source filename, the scalar metrics the
// analyzer emitted (via Analyzer::resultMetrics()), and the human-readable
// summary (Analyzer::resultText()).
struct AnalyzerResult
{
    std::string filename;
    std::unordered_map<std::string, double> metrics;
    std::string detail;
};

// Run the analyzer identified by `analyzerId` over each (filename, frame)
// pair and collect an AnalyzerResult per non-null frame. Null / empty frames
// are skipped. If `analyzerId` is not registered, returns an empty vector.
// Implemented in Analyzer.cpp (needs the registry + concrete analyzers).
std::vector<AnalyzerResult>
runBatch(const std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> &frames,
         const std::string &analyzerId);

} // namespace mviewer::analyzer
