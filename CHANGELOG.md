# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Plugin loading framework (`PluginLoader` + `PluginManager`) with lifecycle management
- UI fixture screenshot regression test (`ui_fixture`)
- AddressSanitizer CI job for memory-leak / UB detection
- Benchmark baseline comparison (`--baseline <csv> --threshold <ratio>`)
- Bicubic and Lanczos interpolation in `RenderEngine`
- Predictive preloading (`ImageRepository::prefetchVisible`)
- Parallel directory loading (`ImageRepository::loadDirectory`)
- Vision regression test framework (`vision_regression`)
- Golden image regression framework (`golden_main`)
- Per-scenario benchmark suite (`benchmark_scenario`)
- `clang-format` configuration and CI formatting check
- **M3 Phase-1 — Core Image Pipeline:**
  - TIFF (`.tif`/`.tiff`) added as a first-class supported format across `Decoder`,
    `FileSystem`, and all UI file filters (decode path is codec-gated; see note below).
  - `ImageRepository::load` now populates the in-memory Viewer/FullImage LRU cache, so
    switching to an adjacent image is instant after the first decode.
  - `ImageViewer` now loads exclusively through `ImageRepository` (no decode logic in the
    QWidget); the histogram is reused from the `ImageFrame` cache, not re-decoded.
  - Pixel Inspector: `ImageViewer` emits `pixelInfo(x,y,r,g,b,valid)` on mouse move, read
    directly from the `ImageFrame` pixels (not `QImage`). Wired to the main-window status bar.
  - `m3pipeline_tests` acceptance suite covering repository→frame, 4-format decode,
    Viewer LRU cache hit, and pixel-inspector reads.

### Changed
- `TaskScheduler` now uses PIMPL to keep Qt threading primitives out of the core header
- `ImageObject` header no longer depends on `QDateTime`
- `CacheManager::diskUsageBytes()` now reports real disk usage via `DiskCache::totalBytes()`
- CI workflow uses portable `${{ github.workspace }}/Qt` paths (no hardcoded `D:\QT`)
- All test/golden paths resolved via `MVIEWER_SOURCE_DIR` (no hardcoded local paths)
- **M3 Phase-1:** `ImageViewer::loadPixmap` refactored to call `ImageRepository::load`
  (removed its private `Decoder`/`CacheManager` decode path and standalone QPixmap LRU).
- **TIFF codec note:** TIFF decode requires the Qt `qtiff` plugin plus an MSVC-built
  `libtiff-6.dll` deployed beside the executable (`imageformats/qtiff.dll`). The format
  pipeline lists TIFF and the `m3pipeline_tests` TIFF case auto-skips when the codec is
  absent, so the suite stays green and TIFF is exercised automatically once the codec ships.

### Removed
- Obsolete `src/analyze_main.cpp` and `src/visual_test.cpp` (hardcoded paths)
- `RenderCommand.h` / `RenderTypes.h` (consolidated into `RenderEngine.h`)

## [0.1.0] - 2026-07-12

### Added
- Initial architecture: 3-panel UI (DirectoryTree, ThumbnailPanel, AnalysisPanel)
- Compare Engine: multi-image sync zoom/pan, blink, difference maps
- Analysis Engine: histogram, PSNR, SSIM, noise, ROI statistics
- Image export: PNG/JPEG/BMP/WebP with quality control
- CacheManager: 5-level cache hierarchy (Metadata → Thumbnail → Preview → Viewer → Disk)
- TaskScheduler: 5 independent priority queues
- Analyzer plugin interface + 7 built-in analyzers
- Core test suite (compare, layout, sync, diff) and unit tests
