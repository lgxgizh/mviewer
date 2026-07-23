#pragma once
#include "core/analyzer/Analyzer.h"

// Color cast analyzer: detects color balance deviation.
// Measures how far the average RGB is from neutral gray.
class ColorCastAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "colorcast";
    }
    std::string description() const override
    {
        return "色偏检测";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "colorcast",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"castR", "castG", "castB", "castMagnitude"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double castR = 0, castG = 0, castB = 0;
        double magnitude = 0;
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
