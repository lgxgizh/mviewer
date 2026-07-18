# ADR-005: Why Plugin-Based Analysis Engine

## Status

Accepted

## Context

Analysis algorithms (histogram, PSNR, SSIM, noise, sharpness, entropy, etc.) grow over time. A monolithic AnalysisEngine with switch-case is unmaintainable.

## Decision

Analysis becomes plugin-based via `Analyzer` interface + `HistogramAnalyzer` registration. Each analyzer is a separate class implementing a common interface.

## Rationale

- **Open/Closed** — add new analyzers without touching existing code
- **Testability** — each plugin tests in isolation
- **Parallelism** — plugins can run concurrently on different threads
- **Lazy registration** — analyzers self-register at startup
- **UI discovery** — registry lists available analyzers dynamically

## Interface

```cpp
class Analyzer {
    virtual string name() = 0;
    virtual bool analyze(const ImageFrame& frame) = 0;
};
```

## Consequences

- ✅ New algorithms without modifying existing code
- ✅ Discovery via registry (no hard-coded list)
- ❌ Plugin ABI stability across versions (future concern)

## Related

- RFC-007 (Analyzer plugin interface)
- RFC-002 (ImageFrame)
