#pragma once
#include "core/analyzer/Analyzer.h"

// Sharpness analyzer: average gradient magnitude (Sobel-like).
// Higher values = sharper image.
class SharpnessAnalyzer : public Analyzer {
public:
  std::string name() const override { return "sharpness"; }
  std::string description() const override { return "锐度 (梯度幅值)"; }

  bool analyze(const ImageFrame &frame) override;
  bool analyzeRegion(const ImageFrame &frame,
                     const mviewer::domain::Selection &region) override;

  double sharpnessValue() const { return m_sharp; }

private:
  double computeSharpness(const ImageBuffer &v, int x0, int y0, int x1,
                          int y1) const;
  double m_sharp = 0.0;
};

namespace {
struct SharpnessAnalyzerRegistrar {
  SharpnessAnalyzerRegistrar() {
    AnalyzerRegistry::instance().registerAnalyzer(
        "sharpness", []() -> std::unique_ptr<Analyzer> {
          return std::make_unique<SharpnessAnalyzer>();
        });
  }
} g_sharpAnalyzerRegistrar;
} // namespace
