#pragma once
#include "core/analyzer/Analyzer.h"

// Sharpness analyzer: average gradient magnitude (Sobel-like).
// Higher values = sharper image.
class SharpnessAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "sharpness";
    }
    std::string description() const override
    {
        return "锐度 (梯度幅值)";
    }

    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "sharpness",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"sharpness"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    double sharpnessValue() const
    {
        return m_sharp;
    }
    std::unordered_map<std::string, double> resultMetrics() const override;

  private:
    double computeSharpness(const ImageBuffer &v, int x0, int y0, int x1, int y1) const;
    double m_sharp = 0.0;
};
