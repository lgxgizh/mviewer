#pragma once
#include "core/analyzer/Analyzer.h"

// Contrast analyzer: RMS contrast (stddev of luminance).
class ContrastAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "contrast";
    }
    std::string description() const override
    {
        return "对比度 (RMS)";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "contrast",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"rmsContrast", "meanLuminance"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double rms = 0;
        double mean = 0;
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
