# MViewer v1.0.0-rc — Release Notes

**Release candidate for the first stable milestone.** MViewer is a visual
analysis platform for image algorithm engineers (ISP / camera / CV): compare,
validate, and analyze image-processing outputs — not a general-purpose viewer.

> Status: **RC**. Architecture is frozen (M3–M10 complete, M9 productization
> verified). This tag exists so the build, test, and packaging pipeline can be
> exercised end-to-end against a pinned version before the final 1.0.0 cut.

## What's in this RC

### Core pipeline (M3)
- `ImageRepository` → `Decoder` → `Cache` → `ImageFrame`. UI never opens a file
  itself; every pixel on screen comes from the repository.
- Formats: JPEG / PNG / BMP / TIFF (TIFF via the Qt `qtiff` plugin).
- In-memory Viewer/FullImage LRU → adjacent-image switching is instant after
  the first decode.

### Compare (M4)
- Multi-image (2–8) side-by-side with **synchronized zoom / pan / selection**,
  **blink** comparison, and **difference heatmaps**.
- Verified by `core_tests` (`ALL_COMPARE_OK`): layout, sync transform, blink,
  diff map, and **non-blocking async diff delivered via the EventBus**.

### Analysis (M4 / M9)
- Registry-driven analyzers (`AnalyzerRegistry`): histogram, RGB mean, PSNR,
  SSIM, noise, entropy, sharpness. ROI analysis consumes a domain `Selection`
  (not `QRect`) — plugins reuse the same path.
- **Pixel Inspector**: hover reads RGB from the `ImageFrame` pixels and shows
  Left / Right / Δ (delta) in real time.
- **Analysis Panel** renders any registered analyzer without custom code.

### Productization (M9)
- 3-panel workflow: DirectoryTree → ThumbnailPanel (sorted) → AnalysisPanel.
- Compare mode (Ctrl+M), Export (Ctrl+S → compare JSON/CSV + diff PNG), Rename
  (F2), Delete, fullscreen (F).
- Keyboard navigation: `Left` / `Right` (prev/next), `Space` (quick preview),
  `F` (fullscreen) — wired via a reusable `CallbackCommand`.
- Verified by `export_tests` (13/13): real `buildCompareReport` + `Encoder`
  produce compare JSON/CSV + diff PNG.

### Performance (M10, B1–B9)
- `MemoryTracker` (Qt-free ledger) samples cache bytes + live `ImageFrame` count.
- Benchmark harness `mviewer_bench` with 9 scenarios; `--enforce` gates:
  - **B2** first thumbnail < 100 ms (verified ~11–20 ms)
  - **B8** preloaded switch < 16 ms (verified p50 ≈ 10 ms)
  - **B9** memory soak: 10 cycles return to baseline, no leak (verified)
- **P1 fix**: `ThumbnailPipeline` enqueues predictive neighbors in the *same*
  pool as visible (after the visible batch) so first-screen priority holds —
  proven by a decode-start-order trace (`priority_by_start=OK`). No Scheduler
  redesign.

## Verification (this RC, real runs)
```
core_tests      → ALL_COMPARE_OK=0, m10_tests ALL PASS, CORE_EXIT=0
export_tests     → 13 passed, 0 failed
mviewer_bench    → B2 11–20ms, B8 10.2ms, B9 baseline_return_ok=1 (--enforce PASS)
MViewer.exe      → builds + links + headless launch, no startup crash
```

## Known gaps (honest)
- CI regression gate (`mviewer_bench --enforce`) is **not** yet wired into
  `ci.yml` (roadmap Phase-4 — deferred so it adds no burden before the
  architecture is stable). Run locally via `build.ps1 Test` + the bench harness.
- Benchmark CI gate deferred; clang-tidy/ASan advisory (non-gating) per the
  phased CI model.
- No installer/CPack packaging yet — the RC ships as a built `MViewer.exe` +
  Qt runtime deployment. (NSIS/WiX installer is a post-1.0 follow-up.)

## Demo dataset
`testdata/golden/` contains graded JPEG/PNG/BMP/TIFF images (64×64 → 8192×8192)
suitable for trying compare / analysis / export immediately.

## Platform
- Windows (MSVC + Qt 6.11.1). Linux build path exists; CI runs on public Qt 6.8.0.
