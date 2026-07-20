#pragma once

#include "core/analyzer/Analyzer.h"

// ColorChecker analyzer.
// Given a ROI that frames a 24-patch ColorChecker (6 columns x 4 rows),
// samples each patch's mean sRGB and reports the mean CIE76 Delta-E
// against the reference patch colors. Used for white-balance / color-accuracy
// validation. If no ROI is supplied it analyzes the whole frame's average
// color against neutral grey (Delta-E to D65) as a coarse sanity check.
class ColorCheckerAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "colorchecker";
    }
    std::string description() const override
    {
        return "色卡/白平衡校验 (Delta-E vs 参考色)";
    }

    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "colorchecker",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"mean_deltaE"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    double meanDeltaE() const
    {
        return m_meanDE;
    }
    int patchCount() const
    {
        return m_patches;
    }
    std::unordered_map<std::string, double> resultMetrics() const override;

  private:
    bool compute(const ImageBuffer &v, int x0, int y0, int x1, int y1);

    double m_meanDE = 0.0;
    int m_patches = 0;
};
