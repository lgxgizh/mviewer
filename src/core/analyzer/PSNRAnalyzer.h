#pragma once
#include "core/analyzer/Analyzer.h"

// PSNR analyzer: peak signal-to-noise ratio between reference and target.
// Set reference via setReference() before calling analyze().
class PSNRAnalyzer : public Analyzer {
public:
    std::string name() const override { return "psnr"; }
    std::string description() const override { return "峰值信噪比 (dB)"; }

    void setReference(const ImageFrame& ref) { m_ref = &ref; }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    double psnrValue() const { return m_psnr; }

private:
    const ImageFrame* m_ref = nullptr;
    double m_psnr = 0.0;
};

namespace {
struct PSNRAnalyzerRegistrar {
    PSNRAnalyzerRegistrar() {
        AnalyzerRegistry::instance().registerAnalyzer(
            "psnr",
            []() -> std::unique_ptr<Analyzer> { return std::make_unique<PSNRAnalyzer>(); });
    }
} g_psnrAnalyzerRegistrar;
}
