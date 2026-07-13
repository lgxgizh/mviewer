# RFC-007: Analyzer Plugin Interface

## Status
Implemented

## Priority
P0

## Goal
AnalysisEngine becomes plugin-based. Adding new analyzers never requires touching AnalysisEngine.

## Interface
```cpp
class Analyzer {
public:
    virtual ~Analyzer() = default;
    virtual string name() const = 0;
    virtual string description() const = 0;
    virtual bool analyze(const ImageFrame& frame) = 0;
    virtual bool analyzeRegion(const ImageFrame& frame, const Selection& region) = 0;
};
```

## Built-in Implementations
- `HistogramAnalyzer` — brightness + RGB histogram
- `RGBMeanAnalyzer` — per-channel mean + stddev
- `NoiseAnalyzer` — Laplacian variance
- `PSNRAnalyzer` — peak signal-to-noise ratio (dual)
- `SSIMAnalyzer` — structural similarity (dual)
- `EntropyAnalyzer` — shannon entropy
- `SharpnessAnalyzer` — gradient magnitude

## AnalyzerRegistry
- Self-register at startup
- Dynamic CRUD (registerAnalyzer, unregister, create, availableAnalyzers)
- Thread-safe (read-heavy, mutex-protected map)

## AnalysisInput/Output
```cpp
struct AnalysisInput {
    ImageFrame& frame;
    optional<Selection> region;
};

struct AnalysisResult {
    string name;
    string displayText;
    optional<ImageData> visualization;
};
```

## Consequences
- New algorithms without code changes
- Dynamic UI discovery
- Parallel analysis execution
