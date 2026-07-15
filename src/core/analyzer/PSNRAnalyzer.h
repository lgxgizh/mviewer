#pragma once
#include "core/analyzer/Analyzer.h"

// PSNR analyzer: peak signal-to-noise ratio between reference and target.
// Set reference via setReference() before calling analyze().
class PSNRAnalyzer : public Analyzer
{
public:
    std::string name() const override { return "psnr"; }
    std::string description() const override { return "峰值信噪比 (dB)"; }
    AnalyzerCapability capabilities() const override {
        return AnalyzerCapability::MultiImage | AnalyzerCapability::QualityMetric;
    }
    AnalyzerInfo info() const override {
        return AnalyzerInfo{
            .id = "psnr", .name = name(), .description = description(),
            .version = "0.1.0", .capabilities = capabilities(),
            .outputFields = {"psnr_dB"}
        };
    }
    void setReference(const ImageFrame& ref) { m_ref = &ref; }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;
    std::string resultText() const override;

    double psnrValue() const { return m_psnr; }

private:
    const ImageFrame* m_ref = nullptr;
    double m_psnr = 0.0;
};
