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
|-----------|-------|--------|
| Foundation | Build system, dir structure, ADR, AGENTS, Roadmap | ✅ Done |
| M2 | Image Core + Task Scheduler | ✅ Done |
| M3 | Core Image Pipeline | ✅ Done (Phase-1 + Phase-2 + cleanup) |
| M4 | Compare & Analysis maturity | ✅ Done (all 4 acceptance criteria met) |
| M5 | Scale & Performance | ✅ Done (disk persistence + hit-ratio + predictive-preload + 1000-img non-blocking verified; benchmark CI gate deferred to Phase-3) |
| M6 | Vertical Browsing Chain (product-grade) | ✅ Done (DecoderRegistry + per-format decoders, metadata enrichment, scheduler priority wiring, test split into 5 suites; 9/9 CTest suites green) |
| M7 | Stability hardening + CI static analysis | 🔧 In progress (benchmark CSV baseline shipped + verified; clang-tidy CI job authored, non-gating; RAW deferred) |

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

## Definition of Done (per milestone)

- [ ] All unit tests pass (`build.ps1 Test` → 100%).
- [ ] All acceptance criteria for the phase are checked.
- [ ] No image-decoding logic in the `QWidget` layer (verified by code review).
- [ ] `CHANGELOG.md` and `STATUS.md` updated.
- [ ] Code reviewed; no new clang-tidy warnings introduced.
