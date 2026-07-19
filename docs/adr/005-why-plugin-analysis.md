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

## ABI Stability Contract (resolved, M13 Phase 6)

The "future concern" is now a concrete, enforced contract (see
`docs/sdk/PLUGIN_SDK.md` §3). Summary:

- **C surface is frozen within a major version:**
  `extern "C" Analyzer* createAnalyzer()`, `void destroyAnalyzer(Analyzer*)`,
  `const char* pluginName()`. No mangling, no version suffix.
- **Toolchain lock:** plugin must build with the same compiler + version and
  the same Qt 6 build as `mviewer_core` (currently MSVC / `c++20` / Qt 6.11).
- **Single shared core:** `mviewer_core` is a SHARED library
  (`WINDOWS_EXPORT_ALL_SYMBOLS`); host and plugin share one `Analyzer` /
  `ImageData` vtable. A static core would duplicate vtables and crash on
  cross-module `delete`.
- **Vtable evolution:** the C++ `Analyzer` interface may add new virtuals only
  at the **end**, each with a default body, so existing overrides stay valid.
- **Lifetime:** plugin owns the instance (allocate in `createAnalyzer`, free in
  `destroyAnalyzer`); host keeps plugins loaded for the app lifetime (no
  runtime unload of Qt-linking DLLs on Windows teardown).
- **Enforcement:** `ctest pluginregistry_tests` (build→load→register→create→
  analyze) plus `pluginloader_tests` / `pluginmanager_tests` gate the contract.

## Related

- RFC-007 (Analyzer plugin interface)
- RFC-002 (ImageFrame)
- `docs/sdk/PLUGIN_SDK.md` — the stable plugin contract
- `plugins/example/` — reference plugin implementation
