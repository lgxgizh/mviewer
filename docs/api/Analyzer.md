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
// Global scope — not inside a namespace (src/core/analyzer/Analyzer.h).
// Base class every analyzer implements.
class Analyzer {
public:
    virtual ~Analyzer() = default;

    static void registerBuiltins();                  // idempotent; called by AnalyzerRegistry::instance()

    virtual std::string name() const = 0;            // stable id, e.g. "histogram"
    virtual std::string description() const = 0;
    virtual bool analyze(const ImageFrame &frame) = 0;                                  // run; stash result internally
    virtual bool analyzeRegion(const ImageFrame &frame,
                               const mviewer::domain::Selection &region) = 0;           // ROI variant

    // Optional hooks, each with a default implementation.
    virtual std::string resultText() const;                                  // human-readable summary
    virtual std::unordered_map<std::string, double> resultMetrics() const;   // machine metrics (batch export)
    virtual AnalyzerCapability capabilities() const;                         // default SingleImage
    virtual AnalyzerInfo info() const;                                       // self-describing metadata
};

// Process-wide registry.
class AnalyzerRegistry {
public:
    // A factory returns an analyzer plus the deleter that owns it, so a plugin
    // can free the instance in its own module.
    using AnalyzerDeleter = std::function<void(Analyzer *)>;
    using AnalyzerCreator = std::function<std::unique_ptr<Analyzer, AnalyzerDeleter>()>;

    static AnalyzerRegistry &instance();
    void registerAnalyzer(const std::string &id, AnalyzerCreator creator);
    void unregister(const std::string &id);

    std::unique_ptr<Analyzer, AnalyzerDeleter> create(const std::string &id) const;
    std::unique_ptr<Analyzer, AnalyzerDeleter> getAnalyzer(const std::string &id) const;  // alias of create()
    std::vector<std::string> availableAnalyzers() const;                     // registered ids

    // Run every registered analyzer on a frame (AnalysisPool, bounded 10 s drain);
    // returns id -> resultText() for analyzers that produced a non-empty text.
    std::unordered_map<std::string, std::string> runAnalyzer(const ImageFrame &frame) const;

    // Run one analyzer by id over many (filename, frame) pairs.
    std::vector<mviewer::analyzer::AnalyzerResult>
    runBatch(const std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> &frames,
             const std::string &id) const;

    AnalyzerCapability capabilitiesOf(const std::string &id) const;
    std::optional<AnalyzerInfo> infoFor(const std::string &id) const;
    std::vector<std::string> queryByCapability(AnalyzerCapability required) const;
};
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

`Analyzer::registerBuiltins()` registers fifteen ids in total. The remaining
(M13/M15) ones are `mtf`, `deadpixel`, `colorchecker`, `brightness`, `contrast`,
`blur`, `colorcast`, `exposure`, implemented by `MTFAnalyzer`,
`DeadPixelAnalyzer`, `ColorCheckerAnalyzer`, `BrightnessAnalyzer`,
`ContrastAnalyzer`, `BlurAnalyzer`, `ColorCastAnalyzer`, `ExposureAnalyzer`.

## Thread-safety
`AnalyzerRegistry` singleton construction is C++11-thread-safe. Registration is
expected at startup (single-threaded init); `runAnalyzer` is read-only on the
registry and safe to call from worker threads. Individual `Analyzer` instances
are created per-call via `create()` / `getAnalyzer()` and are not shared across
threads, so an analyzer may keep per-run state in its members.

## Product flow (review P1 / Scenario C)
`ImageFrame → AnalyzerRegistry::runAnalyzer → AnalysisPanel` renders results.
At least Histogram, RGB Mean, PSNR, SSIM, Sharpness are wired (verified via
`test_analysis_panel.cpp` + manual walkthrough, M12.1).

## Status
✅ Registry is real and populated (satisfies review P1). No change planned for M12.
