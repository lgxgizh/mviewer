#pragma once
#include "core/analyzer/Analyzer.h"

// Entropy analyzer: Shannon entropy in bits/pixel (max 8.0 for random data).
class EntropyAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "entropy";
    }
    std::string description() const override
    {
        return "香农熵 (bits/px)";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "entropy",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"entropy"}};
    }
    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    double entropyValue() const
    {
        return m_entropy;
    }

  private:
    double computeEntropy(const ImageBuffer &v, int x0, int y0, int x1, int y1) const;
    double m_entropy = 0.0;
};
