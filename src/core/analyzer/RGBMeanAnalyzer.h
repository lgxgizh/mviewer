#pragma once
#include "core/analyzer/Analyzer.h"

// RGB mean analyzer: per-channel mean + stddev.
class RGBMeanAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "rgbmean";
    }
    std::string description() const override
    {
        return "RGB 均值/标准差";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "rgbmean",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"rgbMeans", "rgbStd"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double rMean, gMean, bMean;
        double rStd, gStd, bStd;
        bool ok = false;
    };
    const Result &result() const
    {
        return m_result;
    }

  private:
    Result m_result;
};
