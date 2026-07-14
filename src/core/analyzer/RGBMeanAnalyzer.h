#pragma once
#include "core/analyzer/Analyzer.h"

// RGB mean analyzer: per-channel mean + stddev.
class RGBMeanAnalyzer : public Analyzer
{
public:
    std::string name() const override { return "rgbmean"; }
    std::string description() const override { return "RGB 均值/标准差"; }

    bool analyze(const ImageFrame& frame) override;
    bool analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) override;

    struct Result
    {
        double rMean, gMean, bMean;
        double rStd, gStd, bStd;
        bool ok = false;
    };
    const Result& result() const { return m_result; }

private:
    Result m_result;
};

namespace
{
struct RGBMeanAnalyzerRegistrar
{
    RGBMeanAnalyzerRegistrar()
    {
        AnalyzerRegistry::instance().registerAnalyzer("rgbmean",
            []() -> std::unique_ptr<Analyzer> { return std::make_unique<RGBMeanAnalyzer>(); });
    }
} g_rgbMeanAnalyzerRegistrar;
} // namespace
