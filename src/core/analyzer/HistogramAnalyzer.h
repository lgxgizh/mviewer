#pragma once
#include "core/analyzer/Analyzer.h"

// Histogram analyzer: routes to the legacy AnalysisEngine::computeStats
// internally, exposing results as a domain::Histogram.
class HistogramAnalyzer : public Analyzer
{
public:
    std::string name() const override { return "histogram"; }
    std::string description() const override { return "亮度与RGB直方图"; }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    const mviewer::domain::Histogram& result() const { return m_result; }

private:
    mviewer::domain::Histogram m_result;
};
