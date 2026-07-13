#pragma once
#include "core/analyzer/Analyzer.h"

// SSIM analyzer: structural similarity between reference and target.
class SSIMAnalyzer : public Analyzer {
public:
    std::string name() const override { return "ssim"; }
    std::string description() const override { return "结构相似性 SSIM"; }

    void setReference(const ImageFrame& ref) { m_ref = &ref; }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    double ssimValue() const { return m_ssim; }

private:
    const ImageFrame* m_ref = nullptr;
    double m_ssim = 0.0;
};

namespace {
struct SSIMAnalyzerRegistrar {
    SSIMAnalyzerRegistrar() {
        AnalyzerRegistry::instance().registerAnalyzer(
            "ssim",
            []() -> std::unique_ptr<Analyzer> { return std::make_unique<SSIMAnalyzer>(); });
    }
} g_ssimAnalyzerRegistrar;
}
