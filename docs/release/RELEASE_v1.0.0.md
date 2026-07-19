# MViewer v1.0.0 — Release Notes

**First stable milestone.** MViewer is a visual analysis platform for image
algorithm engineers (ISP / camera / CV): compare, validate, and analyze
image-processing outputs — not a general-purpose viewer.

> Status: **RELEASED** (`v1.0.0`, 2026-07-20). Architecture is frozen
> (M3–M10 complete, M9 productization verified, M13 Product-Beta hardening
> complete). This tag supersedes `v1.0.0-rc`; the RC existed only to exercise
> the build/test/packaging pipeline end-to-end against a pinned version.

## What's in 1.0.0

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

### Productization (M9 / M11)
- 3-panel workflow: DirectoryTree → ThumbnailPanel (sorted) → AnalysisPanel.
- Compare mode (Ctrl+M), Export (Ctrl+S → compare JSON/CSV + diff PNG), Rename
  (F2), Delete, fullscreen (F).
- Keyboard navigation: `Left` / `Right` (prev/next), `Space` (quick preview),
  `F` (fullscreen) — wired via a reusable `CallbackCommand`.
- Verified by `export_tests` (13/13): real `buildCompareReport` + `Encoder`
  produce compare JSON/CSV + diff PNG.
- **Product-workflow gate** (`scripts/product_workflow_gate.ps1`): the full
  DirectoryTree → ThumbnailPanel → ImageViewer → CompareWorkspace →
  AnalysisPanel → Export → Workspace chain is verified end-to-end.

### Performance (M10 / M13, B1–B9)
- `MemoryTracker` (Qt-free ledger) samples cache bytes + live `ImageFrame` count.
- Benchmark harness `mviewer_bench` with 9 scenarios; `--enforce` gates:
  - **B2** first thumbnail < 100 ms (verified cold 12–36 ms, warm ≤20 ms)
  - **B8** preloaded switch < 16 ms (verified p50 ≈ 10 ms)
  - **B9** memory soak: 10 cycles return to baseline, no leak (verified)
- **Directory-open is non-blocking**: `loadDirectoryAsync` pre-decodes at
  thumbnail size (`decodeScaled`) so opening a 1000-image directory never
  freezes the UI and never deadlocks the decode pool (see Fix 1 below).

### Packaging (M12 / M13)
- **Portable ZIP** always produced by `release.yml` (tag-triggered).
- **NSIS installer** (`installer/mviewer.nsi`, built via `pack_installer.ps1`)
  produced best-effort by CI; the RC's "no installer" gap is closed.
- `test_package.ps1` validates the produced artifacts.

### Profiling (M13.5)
- Optional Perfetto tracing via `--trace <file>` (self-contained trace sink,
  no external dependency required at build time).

## Fixes landed after the RC (M13 hardening)
1. **Deadlock on large-directory open** (`51c9bc9`): `loadDirectoryAsync`
   fanned out 1000 full-resolution `QImageReader::read()` decodes, which
   deadlock the worker pool under Qt 6.11.1 (offscreen and Windows). Changed
   the pre-decode to `decodeScaled(256px)` — matches the open-dir→thumbnail
   product flow and removes the hang. Verified: `test_m3acceptance` 5/5,
   full gate 31/31.
2. **Qt-leak boundary** (`08e69bf`): removed `#include <QPainter>` from
   `RenderEngine.h` (replaced with a forward declaration). `src/core/**/*.h`
   now contains no `<QWidget>` / `<QPainter>`; the only `<QImage>` is the
   intentional `QtConvert.h` bridge (`.cpp`-only consumers).

## Verification (real runs, 2026-07-20)
```
build.ps1 Test  → 100% tests passed out of 31 (GATE_EXIT=0)
test_m3acceptance → 5 passed, 0 failed (1000-img async open, no block)
mviewer_bench --smoke → B1–B9 ALL PASS (B2 cold 66ms / warm 14ms smoke)
ui_screenshot harness → real MainWindow PNG (1040x700) produced
```

## Known gaps (honest)
- **Demo GIF**: a short UI demo GIF is not yet captured. No `ffmpeg` /
  screen-capture tool is available in the build environment, and the
  `ui_screenshot` harness renders a single static frame. Capture manually
  from the installed app on a GUI session and drop it into
  `docs/release/assets/`. The static `ui_screenshot.png` ships as the
  primary visual.
- Benchmark `--enforce` CI gate is run via `build.ps1 Test` + the nightly
  workflow (`nightly.yml`: clang-tidy / ASan / benchmark, non-blocking),
  not as a PR-blocking gate — by design, to avoid slowing iteration.

## Demo dataset
`testdata/golden/` contains graded JPEG/PNG/BMP/TIFF images (64×64 → 8192×8192)
suitable for trying compare / analysis / export immediately.

## Platform
- Windows (MSVC + Qt 6.11.1). Linux build path exists; CI runs on public Qt 6.8.0.
