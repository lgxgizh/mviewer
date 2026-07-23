#pragma once
#include "core/analyzer/Analyzer.h"

// Exposure analyzer: detects over/under-exposed regions.
// Reports percentage of clipped shadows (<25) and highlights (>230).
class ExposureAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "exposure";
    }
    std::string description() const override
    {
        return "曝光检测 (过曝/欠曝)";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "exposure",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"shadowPct", "highlightPct", "avgLuminance"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double shadowPct = 0;
        double highlightPct = 0;
        double avgLum = 0;
        bool ok = false;
    };
    const Result &result() const
    {
        return m_result;
    }

  private:
    Result m_result;
    bool compute(const ImageBuffer &v, int x0, int y0, int x1, int y1);
};
