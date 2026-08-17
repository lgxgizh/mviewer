# M47 — Source-Backed Display Contract (RFC)

Date: 2026-08-17 · Milestone: M47 · Status: accepted (Phase 0 contract) — **implemented through Phase 6** (Viewer LOD-first display, Compare source-backed panes, exact-source consumers, transactional async restore, soak + benchmark evidence; full CTest gate 104/104). This document remains the normative contract for the display-representation != analysis-source separation; implementation evidence lives in `docs/review/M47_SOURCE_BACKED_DISPLAY_2026-08-17.md`.

## 1. Problem statement

Today both the Viewer and the Compare pane require a **materialized
full-resolution RGB `ImageFrame`** as the precondition for display:

```
Viewer:  File → ImageRepository::load → Decoder::decodeFull → ImageFrame(full RGB)
                                → TileGrid(w,h,256) crops the in-memory full buffer
Compare: File → ImageLoadingService → decodeFull per pane → per-pane full ImageFrame
                                → "display LOD" raster derived client-side from that buffer
```

Verified by code audit at M47 Phase 0:

- `ImageRepository::loadPixels()` (core/image/ImageRepository_load.cpp) always
  calls `Decoder::decodeFull`; `maxEdgeForThumbnail` in `ImageLoadOptions`
  only affects the thumbnail path, never the foreground load.
- `ImageViewer::applyLoadedImage()` builds `m_tiles = TileGrid(width,height,256)`
  from the already-full `m_frame`, then `m_frame` (hundreds of MB for 100 MP)
  is retained while the image is displayed.
- `CompareWorkspace::queueLoadRequests()` loads `decodeFull` frames for every
  pane (`ImageLoadOptions{true,false,256}`), and `displayLodTarget()` derives a
  downscaled raster from each pane's full buffer.

Consequence: opening a 100 MP image and fitting it into a 1500×1000 window
still performs a full decode and retains the full RGB bitmap (~300 MB + ). The
"already has tile rendering" phrasing in earlier docs is misleading — the tile
grid is a client-side crop of a permanently resident full decode, not a
source-driven tiled decode.

## 2. The governing contract

> **Display representation and Analysis source are two different concepts.**
> No implementation may use a display LOD to silently stand in for a
> full-resolution analysis source.

Two independent data planes exist for a `SourceImage`:

| Plane | Consumer | Correctness requirement |
|---|---|---|
| **Display** (LOD/tile/provisional) | View / Compare panes, Fit/Fill/zoom | *visually equivalent* at the rendered density; no promise of exact source pixel values |
| **Source** (exact pixels) | Pixel Inspector, PSNR/SSIM, full diff, Histogram/Analyzers that promise source accuracy, Export | byte-exact within a documented color pipeline; never sampled from an LOD |

Any full-source materialization must be explicit, intentional, cancellable,
async and lifetime-safe — never an implicit side effect of the display
pipeline.

## 3. Capability model (additive, non-breaking)

`DecoderRegistry`/`IDecoder`/plugin ABI stay **frozen** (M47 explicit non-goal:
no registry rewrite, no ABI break). Capabilities are discovered additively:

- A new Qt-free header `core/image/ISourceImageCapabilities.h` defines an
  optional interface. A decoder that implements it (via `dynamic_cast` on the
  `IDecoder*` the frozen registry already stores) advertises:

  1. `probeMetadata` — size/transformation/colorSpace/format **without a full
     pixel decode**.
  2. `decodeLod(maxEdge)` — reduced-resolution decode (native when the backend
     supports it, e.g. `QImageReader::setScaledSize`).
  3. `decodeRegion(rect, targetSize)` — **native** region decode when the
     backend truly supports it.
  4. `cancel` — cooperative cancellation of in-flight work.

- Every capability is **optional**. A decoder without the interface (or a
  decoder that answers "no" for a specific request) falls back to the
  compatible existing path. One format's lack of native region decode must
  never require a destructive interface change.

- `SourceImageProvider` (new, core) is the internal orchestration point:
  resolves a path to a capability-bearing decoder without modifying the frozen
  registry, and classifies each operation path.

### 3.1 Native vs fallback classification (instrumented)

Every decode is classified into exactly one `SourceDecodePath`:

- `ProbeMetadata` — metadata only, no pixels.
- `NativeLod` — backend scaled decode (e.g. Qt `setScaledSize` for JPEG).
- `NativeRegion` — backend true region decode (strip/tile-aware TIFF, JPEG
  DCT-position crop) — **only claimed when actually reliable**.
- `BoundedRasterRegion` — bounded-memory partial raster during decode (e.g.
  Qt `setClipRect`: the allocation is bounded by the region, the CPU may still
  walk the full image). NOT a true native region decode; the source pixel
  values are only guaranteed for the region itself.
- `FullDecodeScaled` — full decode then client-scale (fallback LOD).
- `FullDecodeCrop` — full decode then crop (fallback region).

Classifications are recorded as ATTEMPTS (a failed attempt still shows which
path was taken, plus the `failed` counter); the provider also counts raw
`fullDecode` calls so "number of full-resolution decodes" is directly
observable. Tests must be able to observe which path ran (the
`SourceDecodeStats` counters facade), so a claim like "no full decode for
Fit-to-window" is checkable.

### 3.2 Honest format claims

M47 does **not** claim every format supports true random tile decode. Expected
initial position (to be re-measured at each phase):

- JPEG via `QImageReader`: `NativeLod` for downscale factors the Qt JPEG
  handler supports (1/2, 1/4, 1/8 DCT); `FullDecodeCrop` fallback for arbitrary
  regions.
- TIFF via `QImageReader`: metadata probe; LOD/region generally `FullDecode*`
  fallback unless the backend exposes native strips.
- PNG/BMP: `FullDecode*` fallback for LOD/region (no native tiling).

## 4. Viewer target lifecycle

```
probeMetadata
→ provisional/thubnail preview (from thumbnail cache when available)
→ viewport-appropriate LOD (bounded by output pixels, not source pixels)
→ visible tiles at the current zoom
→ higher-resolution tiles as zoom requires (cooperative cancellation)
```

Invariants:

- Stale tile/LOD must never overwrite a newer generation's display.
- `close`/`destroy` invalidates the consumer token so a late worker can never
  touch a destroyed widget (M46 `AsyncLifetimeToken` machinery is reused).
- Zoom/pan churn cooperatively cancels obsolete work; idle returns
  Decode/Background pools to zero.
- UI thread performs **no** expensive file I/O / full decode / image scaling.
- Tile/cache memory bounded by configured caps.
- The existing small-image fast path (single decode → full ImageFrame) is
  preserved: no regression for ordinary JPEG open latency.

## 5. Compare target lifecycle

- Compare **display** path uses viewport-appropriate LOD/tile per pane; there
  is no requirement that 2/4/8 panes each permanently hold a full RGB source.
- Compare **analysis** path (exact pixel, diff, PSNR/SSIM, histogram,
  Pixel Inspector, export) materializes full (or streamed-region) source
  only on intent, async+cancellable+lifetime-safe, with visible progress for
  perceptible operations and stale-result discard.

## 6. Explicit non-goals (unchanged from M47)

No D3D11/Vulkan/GPU compute rewrite, no Scheduler/CacheManager/DecoderRegistry
rewrite, no new formats, no new analyzer categories, no broad MainWindow
refactor. Anything not needed for this contract stays frozen.

## 7. Acceptance evidence required

- A large-image acceptance/lifetime/benchmark/soak test set (Phase 6) proving
  the display path does not permanently retain a full RGB source when
  capability is available.
- Exact-source consumers (Pixel Inspector, Analyze, diff, PSNR/SSIM, Export)
  verify against the true source, never an LOD.
- Regression suite (96 tests incl. M46 lifetime/soak) stays green.
