# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **M6 — Vertical Browsing Chain:** `DecoderRegistry` (singleton) dispatches files to
  per-format decoders (`QtDecoder` for JPEG/PNG/BMP/TIFF, `QtFallbackDecoder` as last-resort);
  `Decoder` is now a thin shim over the registry. RAW deferred to M7 (`TODO(M7): RAW`).
- `ImageMetadata` enriched with `bitDepth`, `channels`, `colorSpace`, `orientation`
  (EXIF 1-8), `hasIccProfile`, and `format`, populated during decode.
- `ImageRepository::prefetchVisible` submits visible paths at `Priority::UI` (high) and
  adjacent paths at `Priority::Background` (low); M5 DecodePool unlimited-queue fix retained.
- Test suite split: `test_m3m4m5.cpp` broken into `test_decoder`, `test_cache`,
  `test_repository`, `test_scheduler`, `test_metadata` (each its own CTest executable),
  preserving all prior coverage.
- **M7 — Stability hardening + CI static analysis (in progress):**
  - `Benchmark::reportCsv()` emits a machine-readable `benchmark_results.csv`
    (`name,avg_ms,min_ms,max_ms,iterations`); `benchmark.exe` writes it on every run.
    Verified: 11 measurements emitted, CSV round-trips.
  - New non-gating `static-analysis` CI job runs `clang-tidy` (ubuntu, generates its own
    `compile_commands.json` via a clang configure) so the existing green gate is unaffected
    until findings are triaged. Not yet run in CI — first push will surface Qt6-path tuning.
  - RAW decoder remains deferred: `DecoderRegistry` keeps `TODO(M7): RAW` until libraw lands.
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

### Added (M3 Phase-2 — Pixel Inspector panel)
- `AnalysisPanel` gains a **Pixel Inspector** tab that live-displays the hovered pixel:
  coordinates, Left RGB, and (when a second image is loaded) Right RGB / per-channel
  Δ / euclidean distance. Fed by `ImageViewer::pixelInfo` (frame-derived RGB), so the
  primary read still comes from `ImageFrame`, never `QImage`.
- `m3pipeline_tests` now also covers the inspector delta math (zero-delta on identical
  pixels; correct per-channel Δ and euclidean distance).
- **M3 Phase-2 — Selection-driven analysis (registry path):** `AnalysisPanel` now holds
  the left image as an `ImageFrame` (`setFrame`) and routes ROI analysis through
  `AnalyzerRegistry::create("histogram")->analyzeRegion(frame, selection)`, consuming
  `mviewer::domain::Selection` (not `QRect`). The legacy `AnalysisEngine` path is kept
  only as a fallback when no `ImageFrame` is available. `ImageViewer::frame()` exposes the
  backing `ImageFrame` so the QWidget layer decodes nothing.

- **M4 — Analyzer registry is the single UI entry point:** `AnalysisPanel`'s analyzer
  dropdown is now populated from `AnalyzerRegistry::availableAnalyzers()` (histogram,
  noise, entropy, psnr, sharpness, ssim, rgbmean) instead of a hardcoded "histogram".
  Switching the active analyzer — and every ROI analysis — routes through
  `AnalyzerRegistry::create(id)->analyzeRegion(frame, selection)`. Each built-in analyzer
  exposes a generic `resultText()` so the panel renders any registered analyzer without
  custom code. `test_m3m4m5` now asserts all built-ins are creatable via the registry and
  produce a non-empty result.
- **M4 acceptance tests (AC2/AC3):** `testAnalyzerRegistryConsistency` proves the registry
  is the real single entry point — ROI analysis honors an arbitrary `Selection` (left vs
  right half differ) and its results agree with `AnalysisEngine::computeStatsROI` on the
  same region (rgbmean rMean and histogram lumMean within 1.0). This is the core M4 claim
  that `Analyze(selection)` replaces reading `QRect`.
- **M4 — Difference heatmap overlay (compare mode):** `CompareWorkspace` now builds a
  difference heatmap per cell (cell *i* vs base) from the core layer
  (`CompareEngine::differenceMap` → `DifferenceEngine::heatMap`) and hands it to
  `RawImageView::setOverlay` for compositing over the base image with the same
  transform (tracks zoom/pan). The QWidget layer performs no decoding — it only renders a
  QImage the workspace produced from core data. Restores the M4 deliverable that was
  previously dead code (an off-screen canvas that was never blitted). Guarded by
  `testCompareDiffOverlay` in `test_m3m4m5`.

### Added (M5 — Scale & Performance, partial)
- `testCacheManagerM5`: verifies the 5-level cache hierarchy — SQLite-backed disk tier
  persists decoded pixels across a memory clear (byte-identical round-trip, proving the
  disk cache is the durable store / survives restart), and `CacheManager::levelStats`
  reports per-level hit/miss counts with a computed hit ratio.
- `testPredictivePreload`: verifies `ImageRepository::prefetch` warms adjacent images
  from the disk tier into the in-memory FullImage LRU, so navigating to a neighbor is
  instant after cache warm-up (the deterministic core of `prefetchVisible`).
- `test1000ImageNonBlocking`: generates a 1000-image directory and loads it through
  `ImageRepository::loadDirectory` without blocking the UI; verifies all 1000 frames
  return (`Results: 185 passed, 0 failed` on the full M5 suite).
- `testBenchmarkSmokeDecode`: decodes the 4 golden formats (JPEG/PNG/BMP/TIFF) and asserts
  all succeed within budget; now exercises TIFF against the official `qtiff.dll` plugin
  (the format pipeline lists TIFF and the test covers it once the codec ships).
- **Phase-1 CI pipeline** (`ci.yml`): `format` (clang-format + markdownlint) → `build`
  (MSVC + Qt 6.8.0, zero-warning gate) → `test` (ctest) → `package` (artifact zip) →
  `ci-gate` aggregator. No build-system / CMake edits; respects the frozen build contract.

### Fixed (M5 — 1000-image load RCA)
- **Crash (`0xC0000005`) under 1000-image `loadDirectory`**: `DiskCache` shared a single
  `QSqlDatabase` connection (created on the main thread) across all `TaskScheduler` worker
  threads. Qt forbids cross-thread `QSqlDatabase` use → UB → heap corruption. Fixed by
  giving each thread its own `QSqlDatabase` connection to the same SQLite file
  (`DiskCache::connectionForThread`, thread_local, creation serialized by a mutex). The
  shared connection is still used on the owning (main) thread.
- **Hang after the crash fix**: `TaskScheduler` silently dropped tasks exceeding its
  default 1000 queue cap, while `ImageRepository::loadDirectory` busy-waited on a
  completed-task counter that never reached the total. Fixed by setting the DecodePool
  queue depth to unlimited (`setMaxQueueDepth(DecodePool, 0)`) inside `loadDirectory`
  before submitting, so no task is silently dropped for this bounded batch. Both fixed and
  verified green; the defect only manifested at scale (1000 parallel decodes hammering the
  shared connection / exceeding the primed pool cap).

### Added (M6 — Vertical Browsing Chain, product-grade)
- **`DecoderRegistry` + per-format decoders** (`core/image/decoder/`): the single static
  `Decoder` is split into an `IDecoder` interface (Qt-free header, std-only), `QtDecoder`
  (JPEG/PNG/BMP/TIFF via `QImageReader`, EXIF auto-transform, RGB24 output), and
  `QtFallbackDecoder` (last-resort, graceful empty result on failure). `DecoderRegistry`
  (singleton, Qt-free header) dispatches each file to the first decoder whose `canDecode`
  returns true. Unknown formats return an empty `ImageData` (no crash). `Decoder` is kept as
  a thin delegating shim so existing callers keep compiling. RAW is an explicit `TODO(M7): RAW`
  stub (no `libraw` dependency). New images auto-claim via extension; adding a format means
  adding one `IDecoder` — no edits to existing decoders.
- **`ImageMetadata` enrichment** (`domain/Image.h`, Qt-free): added `bitDepth`, `channels`,
  `colorSpace` (sRGB/AdobeRGB/unknown), `orientation` (EXIF 1-8), `hasIccProfile`, and
  `format` (JPEG/PNG/BMP/TIFF). Populated in `QtDecoder` from the decoded `QImage` and merged
  into the `ImageFrame` in `ImageRepository::load` (correct even on a disk-cache hit).
- **Scheduler priority wiring**: `ImageRepository::prefetchVisible` submits visible paths at
  the highest priority and adjacent paths at the lowest; the M5 RCA fix (DecodePool queue
  depth = unlimited inside `loadDirectory`) is retained.
- **Per-module test split**: the monolithic `test_m3m4m5.cpp` (was ~1921 lines) is trimmed
  and its cases redistributed into `test_decoder`, `test_cache`, `test_repository`,
  `test_scheduler`, `test_metadata` (each its own CTest executable via a `foreach` in
  `src/CMakeLists.txt`). All prior coverage preserved — the 1000-image non-blocking test and
  the 4-format golden decode (`ok=4`) still pass.

### Verified
- `build.ps1 Test` → **100% tests passed out of 9** (core_tests, m3m4m5_tests, unit_tests,
  m3pipeline_tests, decoder_tests, cache_tests, repository_tests, scheduler_tests,
  metadata_tests), zero compiler warnings.

### Changed (M4)
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
- **M3 cleanup:** dead `CompareWorkspace::paintEvent` off-screen `canvas` composite
  (base/diff/selection/histogram passes drawn into a `QPixmap` that was never blitted —
  `RawImageView` already paints itself). Also removed the now-unused `m_stats` member,
  `drawCellHistogram` declaration, and `RenderEngine`/`QPainter` includes from the compare
  layer. No behavior change; compare cells still receive the synchronized transform.

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
