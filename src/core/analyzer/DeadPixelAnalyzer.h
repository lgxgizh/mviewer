#pragma once

#include "core/analyzer/Analyzer.h"

// Dead / stuck / hot pixel detector.
// Flags pixels whose value deviates from the local neighborhood median
// by more than `threshold` (in 0..255 luminance units). Reports the
// count and the worst single-pixel deviation. Useful for sensor defect
// screening on flat-field / dark-frame images.
class DeadPixelAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "deadpixel";
    }
    std::string description() const override
    {
        return "坏点/死点检测 (邻域中值偏差)";
    }

    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "deadpixel",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"dead_count", "max_deviation"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    int deadCount() const
    {
        return m_count;
    }
    int maxDeviation() const
    {
        return m_maxDev;
    }

  private:
    bool compute(const ImageBuffer &v, int x0, int y0, int x1, int y1);

    int m_count = 0;
    int m_maxDev = 0;
};
