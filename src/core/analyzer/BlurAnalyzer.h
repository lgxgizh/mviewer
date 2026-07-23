#pragma once
#include "core/analyzer/Analyzer.h"

// Blur analyzer: Laplacian variance blur metric.
// Higher score = sharper image. Lower score = more blurred.
class BlurAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "blur";
    }
    std::string description() const override
    {
        return "模糊检测 (Laplacian 方差)";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "blur",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"laplacianVariance", "sharpnessScore"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double variance = 0;
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
