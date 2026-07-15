#pragma once
#include "core/analyzer/Analyzer.h"

// Noise analyzer: Laplacian variance (lower = smoother, higher = noisy).
class NoiseAnalyzer : public Analyzer
{
public:
    std::string name() const override { return "noise"; }
    std::string description() const override { return "噪声估计 (Laplacian 方差)"; }
    AnalyzerCapability capabilities() const override {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest |
               AnalyzerCapability::StatsOutput;
    }
    AnalyzerInfo info() const override {
        return AnalyzerInfo{
            .id = "noise", .name = name(), .description = description(),
            .version = "0.1.0", .capabilities = capabilities(),
            .outputFields = {"noise"}
        };
    }
    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    double noiseLevel() const { return m_noise; }

private:
    double estimateLaplacian(const ImageBuffer& v, int x0, int y0, int x1, int y1) const;
    double m_noise = 0.0;
};
