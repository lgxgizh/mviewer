# API — Analyzer

**Header**: `src/core/analyzer/Analyzer.h`
**Layer**: core (Qt-free — **no** QWidget / QPainter / QImage dependency)

## Purpose
Pluggable image-analysis subsystem. An analyzer takes an `ImageFrame` (decoded
pixels + metadata) and produces a structured result. Analyzers are registered in
a process-wide `AnalyzerRegistry`; the UI's `AnalysisPanel` queries the registry
and renders whatever results come back — it never calls an analyzer directly
(architectural rule: **QWidget must not call Analyzer**, Analyzer must not depend
on Qt UI types).

## Interface (contract)
```cpp
namespace mviewer::core {

// Base interface every analyzer implements.
class IAnalyzer {
public:
    virtual ~IAnalyzer() = default;
    virtual std::string name() const = 0;            // e.g. "Histogram"
    virtual bool analyze(const ImageFrame &frame) = 0; // run; stash result internally
    // result accessors depend on analyzer type (e.g. histogram bins, scalar metrics)
};

// Process-wide registry.
class AnalyzerRegistry {
public:
    using AnalyzerCreator = std::function<std::unique_ptr<IAnalyzer>()>;

    static AnalyzerRegistry &instance();
    void registerAnalyzer(const std::string &id, AnalyzerCreator creator);
    std::unique_ptr<IAnalyzer> getAnalyzer(const std::string &id) const;
    // Run every registered analyzer on a frame; returns ids that succeeded.
    std::vector<std::string> runAnalyzer(const ImageFrame &frame) const;
    std::vector<std::string> registeredIds() const;
};

} // namespace mviewer::core
```

## Built-in analyzers (`core/analyzer/`)
| Analyzer | File | Output |
|----------|------|--------|
| Histogram | `HistogramAnalyzer.{h,cpp}` | per-channel 256-bin histogram |
| RGB Mean | `RGBMeanAnalyzer.{h,cpp}` | mean R/G/B |
| PSNR | `PSNRAnalyzer.{h,cpp}` | PSNR(dB) vs reference |
| SSIM | `SSIMAnalyzer.{h,cpp}` | structural similarity [0,1] |
| Sharpness | `SharpnessAnalyzer.{h,cpp}` | gradient-based sharpness score |
| Noise | `NoiseAnalyzer.{h,cpp}` | noise estimate |
| Entropy | `EntropyAnalyzer.{h,cpp}` | Shannon entropy |

## Thread-safety
`AnalyzerRegistry` singleton construction is C++11-thread-safe. Registration is
expected at startup (single-threaded init); `runAnalyzer` is read-only on the
registry and safe to call from worker threads. Individual `IAnalyzer` instances
are created per-call via `getAnalyzer` and are not shared across threads.

## Product flow (review P1 / Scenario C)
`ImageFrame → AnalyzerRegistry::runAnalyzer → AnalysisPanel` renders results.
At least Histogram, RGB Mean, PSNR, SSIM, Sharpness are wired (verified via
`test_analysis_panel.cpp` + manual walkthrough, M12.1).

## Status
✅ Registry is real and populated (satisfies review P1). No change planned for M12.
