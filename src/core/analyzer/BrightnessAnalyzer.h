#pragma once
#include "core/analyzer/Analyzer.h"

// Brightness analyzer: average luminance (ITU-R BT.601).
class BrightnessAnalyzer : public Analyzer
{
  public:
    std::string name() const override { return "brightness"; }
    std::string description() const override { return "平均亮度"; }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "brightness",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"avgLuminance", "minLuminance", "maxLuminance"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;
    std::unordered_map<std::string, double> resultMetrics() const override;

    struct Result
    {
        double avgLum = 0;
        double minLum = 255;
        double maxLum = 0;
        bool ok = false;
    };
    const Result &result() const { return m_result; }

  private:
    Result m_result;
    bool compute(const ImageBuffer &v, int x0, int y0, int x1, int y1);
};
