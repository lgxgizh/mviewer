# MViewer Development Roadmap

## Guiding Principles

1. **Stop infrastructure churn** — The build system, CI, presets, and project layout are
   frozen. Do not touch `build.ps1`, `CMakePresets.json`, or `.github/workflows/ci.yml`
   unless explicitly requested.
2. **Milestone cadence over free-form work** — Development proceeds in explicit milestones
   (**M3 → M4 → M5**), each with verifiable acceptance criteria. An Agent must not deviate
   from the milestone's scope or acceptance bar.
3. **Production pipeline before features** — Build the real decode → cache → viewer path
   before menus, toolbars, or chrome.
4. **Domain/Core/UI separation is law** — `domain/` and `core/` headers stay Qt-free. No
   image decoding logic is allowed in the `QWidget` layer; everything goes through
   `ImageRepository`.
5. **No decode in the QWidget layer** — `QWidget` only renders an `ImageFrame`. All decode,
   cache, and preload decisions live in `core/`.
6. **Do not build for architecture's sake** — Implement the minimal real thing that meets the
   acceptance criteria. Do not pre-build SQLite/predict/5-level-cache ahead of the layer that
   needs them.

---

## Milestone status

| Milestone | Theme | Status |
| ----------- | ------- | -------- |
| Foundation | Build system, dir structure, ADR, AGENTS, Roadmap | ✅ Done |
| M2 | Image Core + Task Scheduler | ✅ Done |
| M3 | Core Image Pipeline | ✅ Done (Phase-1 + Phase-2 + cleanup) |
| M4 | Compare & Analysis maturity | ✅ Done (all 4 acceptance criteria met) |
| M5 | Scale & Performance | ✅ Done (disk persistence + hit-ratio + predictive-preload + 1000-img non-blocking verified; benchmark CI gate deferred to Phase-3) |
| M6 | Vertical Browsing Chain (product-grade) | ✅ Done (DecoderRegistry + per-format decoders, metadata enrichment, scheduler priority wiring, test split into 5 suites; 9/9 CTest suites green) |
| M7 | Stability hardening + Render Pipeline foundation | ✅ Done (coverage tests; Perfetto opt-in trace shim; MetadataReader extraction; **Render Pipeline foundation**: `Viewport`+`TileGrid`+`TileCache`(LOD)+tile-based `ImageViewer` paint; **Compare Engine Pixel module** completing Layout/Sync/ROI/Diff/Pixel; **ThumbnailPipeline** subsystem; **Undo/Redo CommandStack**+Rotate/Label; 16/16 CTest green). **CI gate reverted to Phase-1 mandatory** (clang-tidy/ASan advisory, non-gating) per Architect re-prioritization. |
| M8 | Feature completion: Crop + Data Model + Job System + Plugin Registry | ✅ Done (CropCommand reversible; `Workspace→Folder→ImageSet` domain model + `ImageRepository::loadWorkspace`; `Job`/`JobSystem` facade over `TaskScheduler`; **Plugin Registry E2E** — shared `mviewer_core`, real loadable/registerable/queryable `example_analyzer` plugin, subprocess-runner CTest; 20/20 CTest green). |
| M9 | Productization (RFC-driven, scope-disciplined) | ✅ Done (RFC M9_PRODUCTIZATION approved; product-browse / compare-workflow / analysis-panel / export / workspace-persist suites added; CI green on public Qt 6.8.0). |
| M10 | Performance Engineering (MemoryTracker + benchmark harness) | ✅ Done (RFC M10_PERFORMANCE_ENGINEERING approved; `core/perf/MemoryTracker` Qt-free ledger sampling `CacheManager` + live `ImageFrame` count + OS working-set; `benchmark/` 7-scenario suite B1–B7 + standalone `mviewer_bench` harness; `core_tests` folds MemoryTracker + benchmark structural suites; all green. **CI regression gate deferred to Phase-4** per roadmap.) |

> Historical note: an earlier internal scheme reused M3/M4/M5 for the prototype Compare /
> Analysis / Render engines. Those engines are complete and live under `core/compare`,
> `core/analyzer`, and `core/render`. The milestone names below refer to the **current**
> pipeline milestones, not those engines.

---

## M3 — Core Image Pipeline

**Goal:** A production image pipeline where every pixel on screen is served by
`ImageRepository` → `Decoder` → `Cache` → `ImageFrame`, never by a `QWidget` that opened a
file itself.

### M3 Phase-1 — Decode / Cache / Frame (✅ Complete)

Deliverables:

- `ImageRepository::load` returns an `ImageFrame` (pixels + metadata + decode state + histogram).
- JPEG / PNG / BMP / TIFF supported. TIFF requires the Qt `qtiff` plugin plus an MSVC-built
  `libtiff-6.dll` deployed beside the executable; the format pipeline lists TIFF and the
  codec-gated test auto-skips when the codec is absent.
- `ImageRepository::load` populates the in-memory Viewer/FullImage LRU, so adjacent-image
  switching is instant after the first decode.
- Background thumbnail pipeline (directory scan → immediate file list → background thumbnail
  generation → LRU → UI update) already exists and is retained.
- `ImageViewer` loads exclusively through `ImageRepository` (no `Decoder`/`CacheManager`
  decode path, no standalone QPixmap LRU in the widget). Histogram is reused from the
  `ImageFrame` cache.
- Pixel Inspector: `ImageViewer` emits `pixelInfo(x,y,r,g,b,valid)` on mouse move, read
  directly from the `ImageFrame` pixels, wired to the main-window status bar.
- `Selection` domain object exists; analyzers consume `Selection` rather than `QRect`.
- `AnalyzerRegistry` exists with `HistogramAnalyzer` and peers registered.

**Acceptance criteria (M3 Phase-1):**

- [x] Open a directory and load any JPEG/PNG/BMP; `ImageRepository` returns a valid
      `ImageFrame` (no UI decode).
- [x] TIFF is listed by `Decoder`/`FileSystem`/UI filters (decode enabled once `qtiff` +
      `libtiff-6.dll` are deployed).
- [x] Second load of the same path is served from the in-memory Viewer LRU (identical pixels,
      no disk round-trip).
- [x] `ImageViewer` contains no `QImageReader`/`Decoder`/`CacheManager` decode calls.
- [x] Pixel Inspector reports RGB under the cursor, read from `ImageFrame`.
- [x] `m3pipeline_tests` acceptance suite passes (27 checks; TIFF case skips if codec absent).
- [x] **M3 acceptance automated (`test_m3acceptance`):** proves the two P0 bars against the
      real async pipeline — `loadDirectoryAsync` returns in ~15 ms (budget ≤100 ms, i.e. the
      open does NOT block on 1000 decodes) and all 1000 frames decode via the async path; the
      `ThumbnailPipeline` emits the first thumbnail in ~3 ms (budget ≤200 ms). 5/5 checks.
      (Two real bugs in `ImageRepository::loadDirectoryAsync` were found and fixed by this
      suite: a use-after-free from capturing a local `files` vector by reference into worker
      tasks, and a silently-lost completion callback from a context-less `QTimer::singleShot`
      on a worker thread — now marshaled to the app/main thread's event loop.)

### M3 Phase-2 — Sync, Inspector Panel, Selection-driven Analysis (⬜ Next)

Deliverables:

- `CompareEngine` synchronization verified end-to-end through the UI: zoom / pan / scroll /
  selection stay in lock-step across compared cells.
- Pixel Inspector promoted to a live Analysis-panel subscription: mouse move → `ImageFrame`
  read → `pixelInfo` → panel shows Left RGB / Right RGB / Delta / Difference for dual views.
- All analyzers consume the `Selection` domain object (no `QRect` in analyzer signatures);
  `Analyze(selection)` path exercised by the ROI stats UI.
- `AnalyzerRegistry` first analyzers (Histogram / Mean / Noise / PSNR / SSIM / Sharpness /
  Entropy) wired so the plugin registry is the single entry point for analysis.

**Acceptance criteria (M3 Phase-2):**

- [x] Two images support synchronized zoom / pan / scroll / selection in the UI.
- [x] Pixel Inspector displays Left RGB / Right RGB / Difference in real time.
- [x] `Selection` is the sole ROI type passed to analyzers; no `QRect` crosses the core API.
- [x] Switching the active analyzer in the UI routes through `AnalyzerRegistry::create`.
- [x] Regression: all M3 Phase-1 acceptance checks still pass.

### M3 Phase-3 — Thumbnail & Viewer hardening (⬜ Planned)

Deliverables:

- Predictive preload (`ImageRepository::prefetchVisible`) exercised against the viewer's
  navigation so the next/prev image is warm before the keypress.
- Viewer LRU sizing validated against `performance-budget.md` (adjacent switch instant,
  memory within budget).
- Thumbnail pipeline first-paint latency measured and within budget.

**Acceptance criteria (M3 Phase-3):**

- [ ] Navigating next/prev after warm-up is visually instantaneous (no decode spinner).
- [ ] Viewer memory stays within the configured `viewerCacheSize` budget under a 1000-image
      walk.
- [ ] First thumbnail appears within the roadmap latency target.

---

## M4 — Compare & Analysis Maturity

**Goal:** The comparison and analysis feature set is production-grade and plugin-extensible.

Deliverables:

- Dual-image sync (zoom / pan / scroll / selection) hardened for 2–8 images and large
  (50 MP) inputs.
- Analyzer registry complete: `HistogramAnalyzer`, `MeanAnalyzer`, `NoiseAnalyzer`,
  `PSNRAnalyzer`, `SSIMAnalyzer`, `SharpnessAnalyzer`, `EntropyAnalyzer` — all behind the
  unified `IAnalyzer` interface; plugins register the same way.
- Selection-driven analysis: `Analyze(selection)` for mean / variance / noise / PSNR / SSIM.
- Difference heatmap overlay wired into the compare workspace.
- Plugin loader: drop-in analyzer plugins discovered and registered at startup.

**Acceptance criteria (M4):**

- [x] 8-image grid compares with synchronized transform and no UI stall on 50 MP inputs.
- [x] Every built-in analyzer is reachable through `AnalyzerRegistry` and returns results
      consistent with `AnalysisEngine` reference values.
- [x] ROI analysis on an arbitrary `Selection` matches full-image analysis on the same region.
- [x] A sample plugin loads and appears in the analyzer list without code changes.

---

## M5 — Scale & Performance

**Goal:** The pipeline scales to real photo libraries and meets the performance budgets.

Deliverables:

- Open a directory of 1000 images without blocking the UI (scan returns the file list
  immediately; thumbnails stream in).
- First thumbnail appears within ~200 ms of opening a directory.
- Predictive preload tuned to navigation patterns.
- SQLite-backed disk cache (5-level hierarchy: Metadata → Thumbnail → Preview → Viewer →
  Disk) with hit-ratio reporting.
- Benchmark suite in CI with regression gates (`benchmark --baseline --threshold`).
- Memory within `performance-budget.md` limits under sustained navigation.

**Acceptance criteria (M5):**

- [x] 1000-image directory opens without blocking UI; first thumbnail < 200 ms
      (verified: `test_m3m4m5::test1000ImageNonBlocking` loads 1000 images via
      `ImageRepository::loadDirectory` with no UI stall; elapsed ~8.8 s for the full
      directory decode — see RCA note below).
- [x] Switching adjacent images is instant after cache warm-up (predictive preload verified).
- [x] 5-level cache hit ratio reported; disk cache survives restart (verified).
- [ ] Benchmark CI gate fails on regression beyond threshold — **deferred to Phase-3**
      per the (b) decision; not faked here.

> **RCA note (M5 1000-image):** A deterministic crash (`0xC0000005`) and subsequent hang
> were found only under the 1000-image load. Root causes: (1) `DiskCache` shared a single
> `QSqlDatabase` connection across all `TaskScheduler` worker threads → fixed with
> per-thread connections; (2) `TaskScheduler` silently dropped tasks exceeding its default
> 1000 queue cap while `loadDirectory` assumed all submitted tasks ran → fixed by setting
> the DecodePool queue depth to unlimited inside `loadDirectory`. Both fixed and verified
> green (`test_m3m4m5`: 185 passed, 0 failed; `build.ps1 Test`: 4/4 suites).

---

## M6 — Vertical Browsing Chain (product-grade)

**Goal:** Stop horizontal expansion; make ONE vertical chain product-grade. The end-to-end
browse path (scan dir → immediate file list → background thumbnails → click → background full
decode → fast display → next/prev → predictive preload) must be proven, not just assembled.

Deliverables:

- `DecoderRegistry` (singleton, Qt-free header) dispatches each file to the first decoder
  whose `canDecode` returns true. Concrete decoders: `QtDecoder` (JPEG/PNG/BMP/TIFF via
  `QImageReader`, EXIF auto-transform, RGB24 output) and `QtFallbackDecoder` (last-resort,
  claims everything, graceful empty-result on failure). RAW is an explicit `TODO(M7): RAW`
  stub (no `libraw` dependency).
- `Decoder` is kept as a thin delegating shim over `DecoderRegistry` so existing callers keep
  compiling; decode output is unchanged (RGB24 `ImageData`).
- `ImageMetadata` (Qt-free, `domain/Image.h`) enriched with `bitDepth`, `channels`,
  `colorSpace`, `orientation` (EXIF 1-8), `hasIccProfile`, and `format` (JPEG/PNG/BMP/TIFF),
  populated during decode / in `ImageRepository::load`.
- Scheduler priority wiring: `ImageRepository::prefetchVisible` submits visible paths at the
  highest priority (`Priority::UI`) and adjacent paths at the lowest (`Priority::Background`).
  The M5 RCA fix (DecodePool queue depth = unlimited inside `loadDirectory`) is retained.
- Test hygiene: the monolithic `test_m3m4m5.cpp` is split into per-module suites
  (`test_decoder`, `test_cache`, `test_repository`, `test_scheduler`, `test_metadata`), each
  registered as its own CTest executable. All prior coverage is preserved; the 1000-image
  non-blocking test and the 4-format golden decode test (`ok=4`) still pass.

**Acceptance criteria (M6):**

- [x] `DecoderRegistry` dispatches JPEG/PNG/BMP/TIFF to `QtDecoder`; unknown → fallback /
      unsupported (graceful, no crash).
- [x] Decode output identical to before (RGB24 `ImageData`); 4-format golden test `ok=4`.
- [x] `ImageMetadata` carries bitDepth/channels/colorSpace/orientation/format; populated for
      the 4 golden images.
- [x] `build.ps1 Test` green (all CTest suites, including the new split test binaries).
- [x] No image-decoding logic in the `QWidget` layer (decode flows only through
      `ImageRepository` → `DecoderRegistry`).
- [x] `test_m3m4m5.cpp` no longer the single growing mega-file (split done).

---

## M8 — Feature Completion (Crop + Data Model + Job System + Plugin Registry)

**Goal:** Land the four highest-leverage follow-up features the Architect's review
called out, each product-grade and verified by its own CTest suite. No infrastructure /
build-system / CI changes beyond what these features require.

### Deliverables (✅ Complete)

1. **CropCommand** (`core/command/CropCommand` + `core/image/ImageBuffer::cropRegion`)
   - Reversible crop that captures pre-crop pixels for exact undo (mirrors `RotateCommand`).
   - `cropRegion` is a pure-`std` helper: clamps the `Selection` to frame bounds, `memcpy`s
     row-by-row into a new `ImageData`. No Qt in the core path.
   - 14 checks in `test_crop`.

2. **Data Model** (`domain/Workspace`: `Workspace → Folder → ImageSet → ImageFrame`)
   - Pure value types so Compare / Album / Project / Recents compose cleanly.
   - `ImageRepository::loadWorkspace(rootPath, maxPerFolder, recursive)` does a real
     recursive directory scan, groups files by directory into `Folder`/`ImageSet`
     (`ImageMetadata` only, no pixel decode).
   - 12 checks in `test_datamodel` (real temp tree with subdirs `a/` and `b/`).

3. **Job System** (`core/job/Job` — facade over the existing `TaskScheduler`)
   - `Job` / `JobHandle` / `JobSystem` unify Decode / Thumbnail / Analyzer / IO work behind
     one API: submit, cancel, cancel-tree, progress, dependency. The 3 existing pools
     (Decode / Thumbnail / Analysis) are untouched — zero regression.
   - 8 checks in `test_job` (submit, done-callback, progress→100, cancel-stops-early,
     dependency ordering).

4. **Plugin Registry (E2E)** — the registry is now *real*, not a stub
   - `mviewer_core` converted `STATIC → SHARED` (+ `WINDOWS_EXPORT_ALL_SYMBOLS`) so host and
     plugin share one `Analyzer`/`Command` vtable (a static core gave each module its own
     vtable copy → cross-module `dynamic_cast` failures).
   - `AnalyzerCreator` uses a `std::function` deleter so a plugin supplies `destroyAnalyzer`
     for safe cross-module allocation/free.
   - `plugins/example/ExampleAnalyzerPlugin.cpp` is a buildable, loadable analyzer plugin
     (`MeanLuminanceAnalyzer`); exports `createAnalyzer`/`destroyAnalyzer`/`pluginName`.
   - `PluginManager` leak fixed (probe instance deleted after name read); `unload`/`unloadAll`
     intentionally **do not** `FreeLibrary` (plugins are process-lifetime — unloading a
     Qt-linking DLL at teardown crashes on Windows; OS reclaims at exit).
   - CTest uses a **subprocess runner** (`test_pluginregistryrunner` spawns
     `test_pluginregistry`, judges by flushed stdout) to contain the known Windows
     DLL-unload-at-exit crash while still proving load → self-register → create → analyze.

**Acceptance criteria (M8):**

- [x] CropCommand reverts to the original pixels on undo.
- [x] `ImageRepository::loadWorkspace` returns a `Workspace` grouping real files by folder
      (no pixel decode).
- [x] `JobSystem` submits/cancels/orders dependent jobs through the existing scheduler.
- [x] A sample plugin loads, self-registers, and is queryable/creatable through
      `AnalyzerRegistry`; `analyze()` / `analyzeRegion()` return real numbers.
- [x] `build.ps1 Test` green — **20/20 CTest suites** (added crop / job / datamodel /
      pluginregistry / pluginregistryrunner).

> **Build-system note (flagged):** M8 required `mviewer_core` to become a SHARED library so
> the plugin can share its vtable. This is a real, intentional change to `src/CMakeLists.txt`
> (and root `CMakeLists.txt` adds `add_subdirectory(plugins/example)`). It is within the
> feature's authorized scope (making the plugin system real), not a frozen-infra change.

---

## M10 — Performance Engineering (RFC M10_PERFORMANCE_ENGINEERING)

**Goal:** Establish a repeatable, deterministic performance ledger + benchmark harness so
that every future speed/scale claim is backed by a number, not a vibe. Scope is
strictly M9-§8: `MemoryTracker` (Qt-free ledger) + a benchmark suite. No CI
regression gate is wired in (roadmap Phase-4).

**Deliverables (✅ Complete):**

- **`core/perf/MemoryTracker`** — a Qt-free ledger (`MemoryTracker.h` is
  Qt-free; only the `.cpp` OS-RSS read is per-platform guarded). It does **not**
  interpose the allocator (YAGNI — full heaptrack-style interposition is a Phase-4
  concern). It *samples* counters that already exist in `core`:
  - `CacheManager::memoryUsageBytes()` + per-level `levelStats` (hits/misses) →
    `cacheTotalBytes`, `cacheByLevel[4]`, `cacheHits/Misses[4]`;
  - live `ImageFrame` count via additive `ImageFrame` ctor/dtor hooks
    (`notifyFrameCreated/Destroyed`, lock-free atomics, peak tracked);
  - a manual `externalBytes` ledger for in-flight decode buffers held outside the
    cache;
  - a best-effort OS working-set read (never used to fail a budget — OS RSS
    is noisy; budget checks use deterministic bytes + live-frame count).
- **`benchmark/` suite** — 7 scenarios, each returning a structured
  `ScenarioResult{name, metric, value, Timing, detail, passed}`:
  - **B1** startup-to-Qt-ready (event-loop probe when folded into `core_tests`);
  - **B2** first-thumbnail latency (`loadDirectoryAsync` → first thumbnail in cache);
  - **B3** decode latency per format (JPEG/PNG/TIFF p50/p95/p99);
  - **B4** thumbnail throughput (decoded+placed / sec);
  - **B5** cache-hit ratio under Zipf navigation (predictive-prefetch proxy);
  - **B6** memory budget (peak cache bytes; decays after `clearMemory`);
  - **B7** image-switch warm/cold p50.
- **`mviewer_bench`** — standalone harness: `--smoke` (small corpus, exits 0;
  CI gate: proves it links + runs), `--enforce` (applies `docs/performance.md`
  budgets; exits ≠0 on fail — Phase-4 wiring, not yet in `ci.yml`),
  `--corpus-size N`.
- **`core_tests` folds the M10 structural suites** (`MemoryTracker` ledger +
  `benchmark` scenario functors) via `core/test_m10.cpp`, so they run in the
  known-good consolidated exe link environment.

**Acceptance criteria (M10):**

- [x] `MemoryTracker` samples `CacheManager` deterministic bytes + live `ImageFrame`
    count; peak is tracked and `reset()` clears it.
- [x] Benchmark scenarios return well-formed results (finite timing, ratios in [0,1],
    memory decays after clear) — structural, not wall-clock-gated.
- [x] `mviewer_bench --smoke` links and runs end-to-end (exit 0) on a generated
    corpus (JPEG/PNG/TIFF).
- [x] All M10 structural checks pass inside `core_tests` (`build.ps1 Test` → 100%).
- [x] No allocator interposition; OS RSS never fails a budget check.
- [ ] `--enforce` regression gate wired into CI (roadmap Phase-4; intentionally
    deferred so it does not add developer burden before the architecture is stable).

**Follow-up fixes (post-M10, still in this roadmap's scope):**

- **P1 — ThumbnailPipeline priority ordering** (`ThumbnailPipeline::scheduleLocked`):
  neighbors were enqueued at `Priority::Background`, which maps to a *separate*
  `QThreadPool` running concurrently with the `Thumbnail` pool — so on multi-core
  machines background thumbnails could finish before the visible set, violating
  first-screen priority. Fixed by enqueuing neighbors at `Priority::Thumbnail`
  (same pool, after the visible batch) so FIFO drains visible first. Proven by a
  `mviewer_bench --scenario pipeline_priority` trace on decode-*start* order
  (`priority_by_start=OK`). **No Scheduler redesign.**
- **M9 — missing keyboard shortcuts** wired via a new `CallbackCommand`: `Left` /
  `Right` (navigate), `Space` (quick-preview), `F` (fullscreen). Compare sync
  (zoom/pan/blink/diff/ROI), Analysis Panel (registry-driven analyzers + domain
  `Selection` ROI + Pixel Inspector Left/Right/Δ), and Export (compare JSON/CSV +
  diff PNG) are all verified by `core_tests` (`ALL_COMPARE_OK`), `export_tests`
  (13/13), and headless `MViewer.exe` launch.

**Notes / honest gaps:**

- The harness measures the **real `ImageRepository` / `CacheManager` / `Decoder`**
  paths (no mocks), so the numbers reflect actual product behavior on this machine.
- B2/B4 `passed` is informational in `--smoke` (budget enforcement is
  `--enforce`'s job); the scenarios still *report* the metric.
- A latent `paint()` row-offset bug in the corpus generator (wrote pixels with a
  full-image index instead of a per-row index) was found and fixed during M10 —
  it had been silently corrupting the heap and cascading into unrelated Qt-init
  crashes. Root-caused and removed.

---

## Next Phase (post-M7) — Architect re-prioritization (2026-07-16)

The Architect's review re-ranked priorities after M7. Core thesis: **stop infra/static-analysis
expansion; the highest-leverage work is the vertical that every future feature hangs off.**
Agreed-✓ vs. disagreed-✗ from the review:

- ✓ Don't split `ImageRepository` further (YAGNI; it's already a clean façade).
- ✓ `DecoderRegistry` done — must stay the seam for RAW/HEIF/AVIF/OpenCV/GPU later.
- ✓ 5-level cache + priority/cancel/merge/tree-cancel scheduler done.
- ✓ `domain/` stays Qt-free — non-negotiable.
- ✗ Perfetto / clang-tidy-gating / ASan-gating were premature — **demoted** (see CI note).
- ✗ Plugin ABI must NOT be frozen before v1.0 (churn risk).

### P1 (now — DONE in M7)

1. **Render Pipeline** (highest priority). `Image → Tile → Viewport → Renderer → Widget`.
   M7 laid the foundation: `core/render/Viewport` (domain-free pan/zoom) +
   `core/render/TileGrid` (visible-tile enumeration) + `core/render/TileCache` (LRU +
   LOD) + `ImageViewer` paints per visible tile via `RenderEngine::scaleRegion` (no
   whole-image bitmap). Next: tile cache + LOD so 100 MP / RAW render without loading
   the full bitmap into the Widget. *(Note: this "next" was itself completed in M7 —
   LOD + TileCache added. True disk-LOD decode (Decoder emitting reduced-res bitmaps)
   remains a later milestone.)*
2. **Compare Engine decomposition** — split into `Layout` (ViewportController) /
   `Sync` (SyncController) / `ROI` (SelectionController) / `Diff` (DifferenceEngine +
   BlinkController) / `Pixel` (PixelController, added M7). Done; no more monolithic
   CompareController.
3. **Thumbnail Pipeline** — background decode → thumbnail cache → visible queue →
   predictive loading, as its own subsystem (`core/thumbnail/ThumbnailPipeline`, M7).
4. **Undo/Redo** — unified `Command` pattern with `CommandStack` history (undo/redo);
   Rotate + Label commands reversible (M7). *(CropCommand is a follow-up: needs ROI
   pixel extraction; `Selection` domain object exists.)*
5. **Plugin Registry** — Registry/Factory/Metadata fixed now (no ABI freeze).

### P2

- Perfetto, memory benchmark, ASan, clazy (all deferred until architecture stable).

### P3

- Python / Lua / AI / OpenCV plugins.

### Two new directions the review added

- **Data Model**: `Workspace → Folder → ImageSet → ImageFrame` so Compare / Album / Project /
  recents compose cleanly.
- **Job System**: unify Decode / Thumbnail / Analyzer / Benchmark under one
  `Task → Scheduler → Worker → Cancellation → Dependency → Progress` mechanism (the existing
  `TaskScheduler` is the seed).

### CI phasing (Architect directive)

- **Phase-1 (mandatory gate):** Format + Build + Test + Package.
- **Phase-2:** clang-tidy **advisory only** (uploads artifact, never blocks).
- **Phase-3:** ASan (MSVC `/fsanitize=address`) — non-gating signal job.
- **Phase-4:** performance harness + benchmark regression gate.
- **Phase-5:** plugin/AI benchmarks, release automation.
- Perfetto / clazy / ASan are explicitly **deferred** until the architecture is stable; they
  must not add developer burden before then. (M7 temporarily gated clang-tidy/ASan, then
  reverted to this phased model in `f3d3ffa`.)

---

## Definition of Done (per milestone)

- [ ] All unit tests pass (`build.ps1 Test` → 100%).
- [ ] All acceptance criteria for the phase are checked.
- [ ] No image-decoding logic in the `QWidget` layer (verified by code review).
- [ ] `CHANGELOG.md` and `STATUS.md` updated.
- [ ] Code reviewed; no new clang-tidy warnings introduced.
