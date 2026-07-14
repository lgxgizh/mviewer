#pragma once
#include "core/analyzer/Analyzer.h"

// Histogram analyzer: routes to the legacy AnalysisEngine::computeStats
// internally, exposing results as a domain::Histogram.
class HistogramAnalyzer : public Analyzer
{
public:
    std::string name() const override { return "histogram"; }
    std::string description() const override { return "亮度与RGB直方图"; }

    AnalyzerCapability capabilities() const override {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::HistogramOutput | AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override {
        return AnalyzerInfo{
            .id = "histogram", .name = name(), .description = description(),
            .version = "0.1.0", .capabilities = capabilities(),
            .outputFields = {"histogramLuminance", "histogramRGB", "lumMean", "rgbMeans"}
        };
    }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    const mviewer::domain::Histogram& result() const { return m_result; }

private:
    mviewer::domain::Histogram m_result;
};
