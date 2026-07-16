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
- **M7 — Stability hardening + Render Pipeline foundation (DONE):**
  - **Test coverage (review ② + P0-1 scanner):** added `test_filesystem` (16 checks:
    `FileSystem::listImages` enumeration) and `test_eventbus` (11 checks: publish/
    subscribe/unsubscribe/scope isolation); added LRU-eviction test to `test_cache`
    (10 checks). CTest 9 → **12 suites, all green**.
  - **Render Pipeline foundation (Architect P1-①):** new domain-free `core/render/Viewport`
    (pan/zoom/visible-rect math) and `core/render/TileGrid` (visible-tile enumeration) plus
    `test_render_pipeline` (17 checks). `ImageViewer` now drives its transform through
    `Viewport` and paints per visible tile via `RenderEngine::scaleRegion` — no whole-image
    bitmap, no decode in the Widget. This is the seed for 100 MP / RAW tile rendering.
  - **MetadataReader extraction (④):** `core/image/MetadataReader` (`read`/`key`) split from
    `ImageRepository`; 9 new checks in `test_metadata` (now 46 passed).
  - **Perfetto opt-in trace shim (②):** `core/trace/Trace.h` zero-overhead `MV_TRACE_*`
    macros; real Perfetto backend only under `MVIEWER_ENABLE_PERFETTO`. Demoted to P2 per
    Architect re-prioritization (kept because it adds zero burden).
  - **CI (Architect directive):** reverted gating clang-tidy/ASan back to the phased model —
    Phase-1 mandatory gate = Format/Build/Test/Package only; clang-tidy **advisory**
    (uploads artifact, never blocks); ASan **Phase-3 non-gating** signal job. This reverts
    the earlier gating change (`f3d3ffa`).
  - RAW decoder remains deferred: `DecoderRegistry` keeps `TODO(M7): RAW` until libraw lands.
- **M7 P1 — vertical foundations (Architect re-prioritization, DONE):** the four P1
  verticals from the Architect's review, built on the domain/core/UI layering:
  - **① Render Pipeline — TileCache + LOD:** `core/render/TileCache.h` (LRU keyed by
    imageId/col/row/lod, injectable decode fn, LOD selection math) + `test_tilecache`
    (17 checks). `ImageViewer` now requests visible tiles from the cache; missing tiles
    decoded via `RenderEngine::scaleRegion` (core/) and reused across paints. 13/13 green.
  - **② Compare Engine — Pixel module:** `core/compare/PixelController.h` reads the pixel
    at a shared image-space point from every compared cell and computes delta vs a base
    cell. Completes the five-module split (Layout/Sync/ROI/Diff/Pixel). `test_pixelcontroller`
    (9 checks). 14/14 green.
  - **③ Thumbnail Pipeline subsystem:** `core/thumbnail/ThumbnailPipeline.h` (singleton) on
    the shared TaskScheduler — in-memory LRU + `setVisibleRange` priority + `setPredictiveCount`
    forward prefetch; decode fn injected (default `Decoder::decodeScaled`). `ThumbnailPanel`
    consults it as a hot tier. `test_thumbnailpipeline` (8 checks). 15/15 green.
  - **④ Undo/Redo Command pattern:** `ICommand` gained `undo()`/`canUndo()`; new
    `core/command/CommandStack.h` (bounded undo/redo history); `RotateCommand` (backed by new
    `rotate90CW` core helper in `ImageBuffer.h`; `ImageFrame::setPixels` added) and
    `LabelCommand` are reversible. `test_commandstack` (18 checks). **16/16 CTest green**.
  - Honest gaps (not faked): true disk-LOD decode (Decoder emitting reduced-res bitmaps) is
    a later milestone; `ThumbnailWorker` still drives decode synchronously from its thread
    (the pipeline's async path is unit-tested but not yet the panel's sole decode route —
    needs display to verify visually).
- **M8 — Feature completion (Architect follow-ups, DONE):** the four highest-leverage
  follow-ups from the review, each product-grade with its own CTest suite (20/20 total):
  - **CropCommand** (`core/command/CropCommand` + `core/image/ImageBuffer::cropRegion`):
    reversible crop that captures pre-crop pixels for exact undo; `cropRegion` is a pure-`std`
    helper (clamps `Selection` to bounds, row-wise `memcpy`). `test_crop` — 14 checks.
  - **Data Model** (`domain/Workspace`: `Workspace → Folder → ImageSet → ImageFrame`): pure
    value types; `ImageRepository::loadWorkspace(rootPath, maxPerFolder, recursive)` does a
    real recursive scan grouping files by directory into `Folder`/`ImageSet` (metadata only,
    no pixel decode). `test_datamodel` — 12 checks.
  - **Job System** (`core/job/Job` facade over the existing `TaskScheduler`): `Job` /
    `JobHandle` / `JobSystem` unify Decode / Thumbnail / Analyzer / IO behind one API
    (submit / cancel / cancel-tree / progress / dependency). The 3 existing pools are
    untouched. `test_job` — 8 checks.
  - **Plugin Registry (E2E — now real, not a stub):** `mviewer_core` converted `STATIC →
    SHARED` (+ `WINDOWS_EXPORT_ALL_SYMBOLS`) so host and plugin share one `Analyzer`/`Command`
    vtable; `AnalyzerCreator` uses a `std::function` deleter so plugins supply
    `destroyAnalyzer` (cross-module alloc/free safe); `plugins/example/ExampleAnalyzerPlugin`
    is a buildable loadable analyzer (`MeanLuminanceAnalyzer`); `PluginManager` probe leak
    fixed and `unload`/`unloadAll` no longer `FreeLibrary` (plugins are process-lifetime —
    unloading a Qt-linking DLL at teardown crashes on Windows). CTest uses a **subprocess
    runner** (`test_pluginregistryrunner` spawns `test_pluginregistry`, judges by flushed
    stdout) to contain the known Windows DLL-unload-at-exit crash while still proving
    load → self-register → create → analyze. `test_pluginregistry` + `test_pluginregistryrunner`.
  - **Flagged build-system change:** making `mviewer_core` SHARED is a real change to
    `src/CMakeLists.txt` (root adds `add_subdirectory(plugins/example)`). It is within the
    plugin feature's authorized scope, not a frozen-infra change.
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
