# ADR 007: Analyzer plugin interface

## Status
Accepted

## Context
We need multiple analysis algorithms (histogram, RGB, PSNR, SSIM) that can
grow over time without changing call sites.

## Decision
`core/analyzer/Analyzer` is the plugin base (`analyze`, `analyzeRegion`,
`name`, `description`). Implementations self-register a factory in
`AnalyzerRegistry` via a static registrar. `HistogramAnalyzer` currently
wraps the legacy `AnalysisEngine::computeStats` to avoid duplicated logic.

## Consequences
- New analyzers are drop-in (implement + register).
- Registry enables runtime discovery (`availableAnalyzers()`).
