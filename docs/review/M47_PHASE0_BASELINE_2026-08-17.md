# M47 — Phase 0: Baseline, Contract & Failure Reproduction

Date: 2026-08-17 · Milestone: M47 · Status: complete (Phase 0 of 10)

## 1. What Phase 0 had to establish

Before changing any architecture, record (a) exactly which current data paths
materialize a full-resolution RGB bitmap, (b) the quantitative baseline for
large-image behavior, and (c) a deterministic large-image fixture corpus and a
contract fixing that **display representation != analysis source**.

## 2. Data-path audit (code-verified, M47 baseline)

### 2.1 `File -> Decoder -> ImageRepository -> ImageFrame -> ImageViewer`

- `ImageRepository::loadPixels()` (`src/core/image/ImageRepository_load.cpp`)
  always calls `Decoder::decodeFull`; `ImageLoadOptions.maxEdgeForThumbnail`
  only affects the thumbnail path, never the foreground load.
- `ImageViewer::setImage()` → `loadAsyncCancellable` →
  `applyLoadedImage()` (`src/imageviewer_loading.cpp`): builds
  `m_tiles = TileGrid(width,height,256)` **from the already-full `m_frame`** and
  retains `m_frame` while the image is displayed. The "tile grid" is a
  client-side crop of a permanently resident full decode, **not** a
  source-driven tiled decode.
- Pixel Inspector, ROI stats, histogram: sample/derive from
  `m_frame->pixels()` (full source) — correct for exactness, but only available
  after the full materialization.

### 2.2 `File -> ImageLoadingService -> CompareWorkspace -> render LOD/tile`

- `CompareWorkspace::queueLoadRequests()` (`src/compareworkspace.cpp`) loads
  `decodeFull` frames for every pane (`ImageLoadOptions{true,false,256}`); a
  2/4/8-pane compare holds that many full RGB sources.
- `displayLodTarget()`/`scheduleDisplayLodRefresh()`
  (`src/compareworkspace_render.cpp`) derive the display LOD raster
  **client-side from each pane's full buffer**. There is no source-driven LOD.

### 2.3 Decode-category mapping (current implementation)

| Category | Current implementation |
|---|---|
| Thumbnail decode | `Decoder::decodeScaled(maxEdge)` via `ThumbnailPipeline` (Qt `setScaledSize`) |
| Display LOD decode | **none** — display uses the full frame + client-side scale/crop |
| Full-resolution source decode | `Decoder::decodeFull` — the precondition for **every** viewer/compare display |
| Visible tile generation | client-side crop of the full frame (`TileGrid`), not source tiles |
| Analysis full-resolution source | the same full `ImageFrame` as display (shared) |
| Pixel Inspector source | `m_frame->pixels()` (full) |
| Export source | full `ImageFrame`/`ImageData` |

Conclusion (recorded, not asserted): **every current display path requires the
full-resolution RGB bitmap to already exist.**

## 3. Deterministic large-image fixture corpus

New tracked generator `testdata/generate_large_fixtures.py` (Pillow, idempotent,
`--check`/`--ensure`/`--force`; output under git-ignored `testdata/large/`):

| Fixture | Dims | Notes |
|---|---|---|
| `large_jpeg_100mp.jpg` | 12000×8333 | ~100 MP JPEG q80 |
| `large_tiff_100mp.tiff` | 10000×10000 | ~100 MP TIFF LZW |
| `high_compression.jpg` | 6000×4000 | JPEG quality 4 (heavy artifacts) |
| `exif_orientation6.jpg` | 4096×4096 | EXIF orientation 6 |
| `exif_orientation8.jpg` | 4096×4096 | EXIF orientation 8 |
| `icc_adobe.jpg` | 2048×2048 | embedded ICC (sRGB) |
| `extreme_wide.jpg` | 20000×400 | extreme landscape |
| `extreme_tall.jpg` | 400×20000 | extreme portrait |
| `truncated_large.jpg` | 6000×4000 | valid file truncated ~55% |
| `truncated_large.tiff` | 4000×4000 | valid file truncated ~50% |

Deterministic pixel content (NEAREST-resized base pattern with orientation
corner markers). Registered as CTest `large_fixture_gate` (validate dims).

## 4. Baseline measurements (before any Phase 1 change)

Recorder: `benchmarks/m47_large_image_baseline_main.cpp` (CTest
`m47_large_image_baseline`, JSON →
`benchmark/report/large_image_baseline_phase0.json`). Instrumentation:
benchmark-local counting decoder registered ahead of QtDecoder (same pattern as
the M46 test suite). Machine: 4-logical-core VM, offscreen.

### 4.1 Repository path (`File -> ImageRepository -> ImageFrame`)

| Fixture | Mode | Latency | Output | RGB bytes | RSS Δ | decodeFull | decodeScaled |
|---|---|---|---|---|---|---|---|
| 100MP JPEG | full-cold | 10.7 ms | **0×0 FAIL** | 0 | +0.9 MB | 1 | 0 |
| 100MP JPEG | scaled-256 | 111.9 ms | 256×177 | 136 KB | +0.5 MB | 0 | 1 |
| 100MP TIFF | full-cold | 2.0 ms | **0×0 FAIL** | 0 | +0.1 MB | 1 | 0 |
| 100MP TIFF | scaled-256 | 1.4 ms | **0×0 FAIL** | 0 | +0.0 MB | 0 | 1 |
| 24MP JPEG q4 | full-cold | 820.4 ms | 6000×4000 | 72 MB | +70.9 MB | 1 | 0 |
| 24MP JPEG q4 | scaled-256 | 25.8 ms | 256×170 | 131 KB | +0.4 MB | 0 | 1 |
| 8MP extreme-wide | full-cold | 284.7 ms | 20000×400 | 24 MB | +23.1 MB | 1 | 0 |
| 8MP extreme-wide | scaled-256 | 11.7 ms | 256×5 | 3.8 KB | −0.2 MB | 0 | 1 |
| 8MP extreme-tall | full-cold | 264.7 ms | 400×20000 | 24 MB | +22.9 MB | 1 | 0 |
| 8MP extreme-tall | scaled-256 | 10.7 ms | 5×256 | 3.8 KB | −0.0 MB | 0 | 1 |
| 100MP JPEG | full-warm | 0.9 ms | **0×0 FAIL** (nothing cached) | 0 | +0.0 MB | 1 | 0 |

### 4.2 Viewer path (`ImageViewer::setImage` → first usable frame)

| Scenario | Result |
|---|---|
| 24MP open→first frame | **1020–1030 ms**; frame = full 6000×4000; **+243.6 MB RSS while held**; 1 full decode |
| UI-thread repaint (sync) | **fit 147.6 ms · 100% 164.0 ms · zoomIn 150.8 ms · refit 126.1 ms** — the paint path scales the full 72 MB frame on the UI thread |
| Repeated open/close (warm) | ~0.98–1.12 s per open; RSS +64.8…98.7 MB per cycle (cache + frame retention) |
| 100MP open | **cannot open** — Qt rejects the full decode |

### 4.3 Key findings

1. **Hard reproduction (the milestone's core problem, proven):** Qt 6.10's
   default `QImageIOHandler` allocation limit (256 MB) rejects full decodes of
   images whose RGB materialization exceeds it. The 100 MP JPEG **and** 100 MP
   TIFF **cannot be opened at all** by the current Viewer/Repository path
   (`qt.gui.imageio: Rejecting image as it exceeds the current allocation limit
   of 256 megabytes` → `Decoder::decodeFull` returns null).
2. **JPEG has a working reduced-resolution decode today:** the 100 MP JPEG
   scaled-256 succeeds in ~112 ms (Qt `setScaledSize` → libjpeg scaled read).
   This is the seed the Phase-1/2 "viewport LOD" path can build on.
3. **TIFF scaled decode also fails** (Qt still needs the full raster) — TIFF
   LOD will need the native-vs-fallback classification; no false claims.
4. **Fit-to-window materializes the full frame:** 24 MP open retains 72 MB RGB
   (+243.6 MB process RSS), and every repaint (fit/100%/zoom) stalls the UI
   thread 126–164 ms scaling that buffer.
5. **Cache warm does not help large images:** the 100 MP full decode never
   succeeds, so nothing is cached; warm reload still attempts a full decode.
6. **Compare holds N full frames** for N panes (same materialization, no
   bounded LOD per pane today).

## 5. Contract (RFC)

`docs/rfc/M47_SOURCE_BACKED_DISPLAY.md` fixes:

> **Display representation and Analysis source are two different concepts.**
> No implementation may use a display LOD to silently stand in for a
> full-resolution analysis source.

plus the additive capability model (optional probe/LOD/region/cancel interfaces
discovered without touching the frozen `DecoderRegistry`/`IDecoder` ABI), the
native-vs-fallback classification (`NativeLod` / `NativeRegion` /
`FullDecodeScaled` / `FullDecodeCrop` / `ProbeMetadata`), instrumentation
requirements, and honest per-format expectations.

## 6. Deferred to Phase 6 gates (by milestone plan)

- Event-loop responsiveness probe under pan/zoom churn (Phase 6 gate).
- Decode-worker occupancy and idle convergence under churn (Phase 6 gate; M46
  soak already covers idle convergence at the product level).
- Compare 2/4/8-pane large-image measurements (Phase 3/6).

## 7. Artifacts

- `testdata/generate_large_fixtures.py` + `large_fixture_gate` (CTest).
- `benchmarks/m47_large_image_baseline_main.cpp` +
  `m47_large_image_baseline` (CTest) + JSON evidence.
- `docs/rfc/M47_SOURCE_BACKED_DISPLAY.md`.
- This document.
