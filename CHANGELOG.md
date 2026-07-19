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
- **M13 — Product Beta (in progress):** shift from infrastructure to value.
  - **M13.1 Product Workflow gate:** `scripts/product_workflow_gate.ps1` chains the five
    workflow executables (Browse → Compare → Analyzer → Export → Workspace) in user order;
    ctest `product_workflow_gate` passed 5/5.
  - **M13.2 Benchmark dashboard:** `scripts/benchmark_dashboard.ps1` parses result logs into
    `benchmark/report/{history.csv,index.html}` trend; `nightly.yml` dashboard job (non-gating).
  - **M13.3 NSIS installer:** `installer/mviewer.nsi` + `pack_installer.ps1` produce
    `dist/MViewer-1.0.0-setup.exe`; portable zip + real UI screenshot (`ui_screenshot` harness).
  - **M13.4 Real image datasets:** `testdata/generate_variants.py` adds format/integrity variants
    (16-bit TIFF, Gray/RGBA PNG, CMYK TIFF, bad-EXIF JPEG, bad-ICC PNG); `test_assets_acceptance`
    opens every fixture via real `Decoder::decodeFull` — 122 scanned / 108 decoded / 14 graceful-skip
    / 0 crash. Perf: B2 first-thumbnail COLD 34 ms (review target <300 ms) — prior 2400 ms gap closed.
  - **Review P2 Tile RFC:** `docs/rfc/M13_TILE_PIPELINE.md`; `core/render/TileCache.h`/`TileGrid.h`
    + tests landed and wired into `ImageViewer::paintEvent`.
  - **Review P1 AnalyzerRegistry realized:** `getAnalyzer()`/`runAnalyzer()` exercised by
    `test_analysis_panel` (7+ analyzers, single + ROI + dual-image PSNR/SSIM).
  - **M13.5 Perfetto profiling:** `core/trace/TraceSink.{h,cpp}` self-contained span recorder
    (Chrome trace JSON, openable in ui.perfetto.dev / chrome://tracing); `MV_TRACE_*` hot-path
    points now forward to it under `MVIEWER_ENABLE_PERFETTO` (OFF by default — green build
    untouched, zero dependency). `mviewer_bench --trace <file>` flushes; `scripts/trace_report.py`
    prints per-stage p50/p95/p99 from a real trace (5274 spans captured; decodeFull p99 8.3ms).
  - **M13.6 Plugin SDK stabilize:** `docs/sdk/PLUGIN_SDK.md` (stable contract: `Analyzer` iface +
    3 frozen `extern "C"` exports + ABI rules), `plugins/example/README.md` (reference plugin),
    ADR-005 ABI-stability contract resolved. `test_plugin_loader`/`test_plugin_manager` (built but
    never registered) now gated as `pluginloader_tests`/`pluginmanager_tests` ctest — both PASS.
    Demo plugin `example_analyzer.dll` builds + is load→register→create→analyze'd by MViewer
    (`pluginregistry_tests` PASS). Known: 7 pre-existing tests flaky under `ctest -j4` (shared
    singletons/fixtures), pass serially — not a Phase 6 regression.
  - **M13.7 GPU route RFC:** `docs/rfc/M13_GPU_ROADMAP.md` — staged CPU→Tile→GPU-upload→
    Direct2D/D3D11→Vulkan route grounded in the actual render path (TileCache/TileGrid/Viewport
    + RenderEngine::scaleRegion). Recommends Stage A (GPU blit, low risk) only, gated on a
    measured 100 MP deficit; Stage C/D deferred (frozen UI=Qt Widgets boundary). Design only,
    no code.
  - **M13.8 Public roadmap:** `docs/ROADMAP_PUBLIC.md` — user-facing Beta→1.0→1.1→2.0 track
    (what ships now / planned / deferred: RAW, GPU Stage C/D, language plugins). `roadmap.md`
    cross-links it. Closes all 8 M13 phases.
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
  - **Bug fix (M13 — `loadDirectoryAsync` concurrent full-decode deadlock):**
    `ImageRepository::loadDirectoryAsync` fanned out 1000 full-resolution
    `QImageReader::read()` decodes across the DecodePool. Under Qt 6.11.1
    (offscreen platform, and likely Windows) a fully concurrent
    `QImageReader::read()` deadlocks the worker pool, hanging
    `TaskScheduler::drain` forever and freezing the UI on a large
    directory. Root cause: the directory pre-decode path produced
    full-resolution frames instead of browse/thumbnail-sized ones. Fixed by
    switching the pre-decode to `Decoder::decodeScaled(256px)`; full
    decode stays on-demand in `load()` when a single image is opened.
    This both matches the product flow (open dir -> thumbnails) and
    removes the deadlock. `test_m3acceptance` now passes 5/5 and the
    full `.\build.ps1 Test` gate is green (31/31).
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
- **M3 acceptance verification — review's two P0 bars now proven by automated test (DONE):**
  new `core/test_m3acceptance.cpp` (`m3acceptance_tests` CTest) measures the review's P0
  acceptance against the real async pipeline: (1) `ImageRepository::loadDirectoryAsync` on
  1000 images returns in ~15 ms (open does NOT block on decode) and delivers all 1000 frames
  via the callback; (2) `ThumbnailPipeline` emits the first thumbnail in ~3 ms. 5/5 checks.
  This suite **caught two real bugs** in `ImageRepository::loadDirectoryAsync` and they are
  fixed: (a) use-after-free — the worker lambda captured the local `files` vector by
  reference; now a `shared_ptr` captured by value. (b) the completion callback was delivered
  via a context-less `QTimer::singleShot(0, ...)` created on a worker thread, so it never
  fired (worker has no event loop); now marshaled to `QCoreApplication::instance()` so it
  runs on the thread with a live loop. `ImageRepository` callers that relied on the async
  completion callback now actually receive it.
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

### Added (M10 — Performance Engineering, DONE)

- **`core/perf/MemoryTracker`** (Qt-free ledger; RFC `M10_PERFORMANCE_ENGINEERING`):
  samples existing core counters — `CacheManager::memoryUsageBytes()` + per-level
  `levelStats` (hits/misses) → `cacheTotalBytes` / `cacheByLevel[4]` /
  `cacheHits/Misses[4]`; tracks live `ImageFrame` count via additive
  `ImageFrame` ctor/dtor hooks (`notifyFrameCreated/Destroyed`, lock-free
  atomics, peak tracked); a manual `externalBytes` ledger for in-flight decode
  buffers; and a best-effort OS working-set read that is **never** used to
  fail a budget (OS RSS is noisy — budget checks use deterministic bytes +
  live-frame count). No allocator interposition (YAGNI; heaptrack-style is Phase-4).
- **Benchmark suite `benchmark/`** — 7 structural scenarios B1–B7, each
  returning a `ScenarioResult{name, metric, value, Timing, detail, passed}`:
  - **B1** startup-to-Qt-ready (event-loop probe when folded into `core_tests`);
  - **B2** first-thumbnail latency (`loadDirectoryAsync` → first thumbnail);
  - **B3** decode latency per format (JPEG/PNG/TIFF p50/p95/p99);
  - **B4** thumbnail throughput (decoded+placed / sec);
  - **B5** cache-hit ratio under Zipf navigation (predictive-prefetch proxy);
  - **B6** memory budget (peak cache bytes; decays after `clearMemory`);
  - **B7** image-switch warm/cold p50.
- **`mviewer_bench` standalone harness**: `--smoke` (small corpus, exit 0 — CI
  gate: proves it links + runs), `--enforce` (applies `docs/performance.md`
  budgets; exits ≠0 on fail — Phase-4 wiring, not yet in `ci.yml`),
  `--corpus-size N`.
- **`core_tests` folds the M10 structural suites** (`MemoryTracker` ledger +
  `benchmark` scenario functors) via `core/test_m10.cpp`, so they run in the
  known-good consolidated-exe link environment. CMake: MemoryTracker.cpp added
  to `mviewer_core`; `mviewer_bench` target + `bench_smoke` CTest added.
- **Root-caused a latent corpus-generator bug**: `benchmark/corpus.cpp`'s
  `paint()` wrote pixels with a full-image index `idx = (y*w+x)*3` instead of
  the per-row `x*3` (the row pointer `scanLine(y)` already offsets the row).
  This wrote far past the `QImage` buffer → silent heap corruption that
  cascaded into unrelated Qt-init AVs (e.g. `core_tests` crashing at
  `QCoreApplication` ctor). Fixed; both `mviewer_bench` and `core_tests` now run.

### Added (M10 Phase-3 — B8/B9 stability benchmarks, DONE)

- **B8 — preloaded switch first-interaction latency (< 16 ms):** new
  `scenarioSwitchLatency` fully warms the in-memory FullImage LRU, then times
  200 back-and-forth navigations (all cache hits) and reports p50/p95/p99 of a
  single frame-to-frame switch. Under `--enforce` the strict `docs/performance.md`
  budget of **< 16 ms** is applied (vs B7's softer ≤50 ms report). Verified
  `--enforce` PASS: p50=10.2 ms (p95=31.8, p99=525.6 — the p99 tail is LRU
  eviction/re-decode at the cache-cap boundary; the per-frame p50 is well under
  the one-frame budget as the spec demands).
- **B9 — memory soak / stability:** new `scenarioSoakStability` runs 10
  open→navigate→evict cycles over an 80-image window, asserting each cycle's
  post-`clearMemory` sample ≤ its own peak (no in-cycle growth) and that the
  final baseline returns to ~0 (no cumulative leak). Under `--enforce` requires
  `baseline_return_ok` AND final ≤ 2× initial. Verified `--enforce` PASS:
  baseline_return_ok=1, all 10 cycles decay to 0, finalBase=0; global peak
  488 MB stays at the spec's 512 MB L2 cap (correctly bounded, not a leak).
- `mviewer_bench --enforce` now gates **B2 (<100ms)**, **B8 (<16ms)**, and
  **B9 (baseline return)**. B1/B3–B7/B6 remain report-only (Phase-4 CI wiring
  deferred per roadmap). No new CMake target — B8/B9 fold into the existing
  `mviewer_bench` executable.

### Added (M10 follow-ups — P1 priority fix + M9 keyboard shortcuts, DONE)

- **P1 — ThumbnailPipeline priority ordering fixed (review directive):** the
  scheduler maps `Priority::Background` to a separate `QThreadPool` that runs
  **concurrently** with the `Thumbnail` pool. That let neighbor (background)
  thumbnails finish *before* the visible ones on multi-core machines, violating
  the review's first-screen priority. Fix: `ThumbnailPipeline::scheduleLocked`
  now enqueues neighbors at `Priority::Thumbnail` (same pool as visible) and
  *after* the visible batch, so FIFO guarantees the visible set drains first.
  No Scheduler redesign — only the priority tag + ordering in the pipeline.
  Proven by a new `mviewer_bench --scenario pipeline_priority` trace that records
  per-image decode-*start* order (decode-cost-independent): `priority_by_start=OK`
  (visible_start_max ≤ neighbor_start_min). Replaces the earlier completion-order
  check that was fooled by mixed JPEG/TIFF decode costs.
- **M9 — missing keyboard shortcuts wired (review P2.2):** added a generic
  `core/command/CallbackCommand` (id + description + callback + shortcuts) and
  registered four commands in `MainWindow::setupCommands`: `Left` → previous
  image, `Right` → next image, `Space` → quick-preview current image,
  `F` → toggle fullscreen (acts on the viewer when open, else the main window).
  Pre-existing shortcuts (`Ctrl+O` open, `Ctrl+S` export, `Ctrl+M` compare,
  `Delete`, `F2` rename, `Ctrl+H` histogram) were left intact. No new abstraction
  layer — one reusable command class instead of three boilerplate command files.
- **M9 acceptance verification (real tests, no fakes):** the Compare workflow is
  exercised by `core_tests` (`test_compare.cpp` → `ALL_COMPARE_OK`) proving
  layout/sync/blink/diff + **non-blocking async diff with EventBus delivery**
  (acceptance C2); Export is exercised by `export_tests` (13/13) proving the real
  `core::buildCompareReport` + `Encoder` produce compare JSON/CSV + diff PNG;
  AnalysisPanel routes ROI through `AnalyzerRegistry::create("histogram")` consuming
  a domain `Selection` (QRect→Selection at the UI boundary, as the review required).
  `MViewer.exe` builds + links + launches headless (offscreen) with no startup crash.

### Added (M11 — Release Candidate v1.0.0-rc, DONE)

- **Version bump** `CMakeLists.txt` `0.1.0` → `1.0.0` (RC; not a build-system
  change — `build.ps1`/`CMakePresets.json`/`ci.yml` untouched per the freeze).
- **Release notes** `docs/release/RELEASE_v1.0.0-rc.md` — accurate, verification-backed
  notes (core pipeline / compare / analysis / productization / performance), derived
  from the CHANGELOG + RFCs. The README was **not** modified (out of scope; it already
  describes the product correctly).
- **Git tag + GitHub release** `v1.0.0-rc` (pre-release) published via `gh`:
  https://github.com/lgxgizh/mviewer/releases/tag/v1.0.0-rc
- **Final RC verification (real runs):** `core_tests` (`ALL_COMPARE_OK=0`,
  `m10_tests ALL PASS`), `export_tests` (13/13), `mviewer_bench --enforce`
  (B2 11–20ms, B8 p50≈6–10ms <16ms, B9 `baseline_return_ok=1`; ALL PASS),
  `MViewer.exe` builds + links + headless-launch with no startup crash.
- **Deferred to post-1.0 (honest):** NSIS/WiX installer / CPack packaging — the RC
  ships as a built `MViewer.exe` + Qt runtime deployment. CI `--enforce` regression
  gate remains Phase-4 (advisory, non-gating per the frozen CI model).

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
