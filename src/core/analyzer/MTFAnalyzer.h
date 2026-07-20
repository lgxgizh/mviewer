#pragma once

#include "core/analyzer/Analyzer.h"

// MTF (Modulation Transfer Function) analyzer.
// Measures spatial resolution from an edge transition in the image:
//   ESF (edge spread) -> LSF (line spread, d/distance) -> FFT -> MTF.
// Reports MTF50: the spatial frequency (normalized to Nyquist = 1.0)
// at which contrast falls to 50% of the low-frequency value.
// Higher MTF50 = sharper lens/sensor.
class MTFAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "mtf";
    }
    std::string description() const override
    {
        return "调制传递函数 (MTF50 空间频率)";
    }

    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "mtf",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"mtf50", "mtf50_cycles_per_px"}};
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    double mtf50() const
    {
        return m_mtf50;
    }
    // MTF50 in cycles per pixel (Nyquist = 0.5).
    double mtf50CyclesPerPixel() const
    {
        return m_mtf50Cps;
    }

  private:
    // Compute MTF50 over the given pixel rectangle. Returns true on success.
    bool compute(const ImageBuffer &v, int x0, int y0, int x1, int y1);

    double m_mtf50 = 0.0;    // normalized to Nyquist (0..1]
    double m_mtf50Cps = 0.0; // cycles per pixel
};
