#pragma once
#include "core/analyzer/Analyzer.h"

#include <optional>

// PSNR analyzer: peak signal-to-noise ratio between reference and target.
// Set reference via setReference() before calling analyze().
// The analyzer stores its own copy of the reference so the caller does not
// need to keep the original alive.
class PSNRAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "psnr";
    }
    std::string description() const override
    {
        return "峰值信噪比 (dB)";
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::MultiImage | AnalyzerCapability::QualityMetric;
    }
    AnalyzerInfo info() const override
    {
        return AnalyzerInfo{.id = "psnr",
                            .name = name(),
                            .description = description(),
                            .version = "0.1.0",
                            .capabilities = capabilities(),
                            .outputFields = {"psnr_dB"}};
    }
    void setReference(const ImageFrame &ref)
    {
        m_ref = ref;
    }

    bool analyze(const ImageFrame &frame) override;
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region) override;
    std::string resultText() const override;

    double psnrValue() const
    {
        return m_psnr;
    }
    std::unordered_map<std::string, double> resultMetrics() const override;

  private:
    std::optional<ImageFrame> m_ref;
    double m_psnr = 0.0;
};
