# RFC — Tile Render Pipeline (M11 Product Beta, P2)

**Status:** DRAFT for review (planning only — no implementation until approved).
**Scope:** CPU tile-based rendering for very large images (100 MP+ / 12000×8000),
so zoom/pan never blocks the UI and only visible tiles are decoded. **First
version is CPU-only — no GPU.** This formalizes the M7 `Viewport`/`TileCache`/
`TileGrid` foundation into a deliverable pipeline.

## Problem
Today `ImageViewer` paints the whole `ImageFrame` through `QPainter` (M3/M7).
For 4096×3072 this is fine. For 100 MP RAW / 12000×8000 the entire decoded bitmap
(~360 MB RGBA) must be held and re-scaled on every paint → memory pressure, zoom
jank, and a long first-decode wait. The review (P2) wants a tile pipeline before
GPU work.

## Existing foundation (M7, reuse — do not rebuild)
- `core/render/Viewport` — pan/zoom/visible-rect math (domain-free).
- `core/render/TileGrid` — visible-tile enumeration from a Viewport.
- `core/render/TileCache` — LRU keyed by (imageId, col, row, lod), injectable
  decode fn, LOD selection. Already has `test_tilecache` (17 checks).

## Proposal (CPU, v1)
```
Image (ImageFrame, full-res on disk/cache)
   │  TileManager.load(imageId, viewport)
   ▼
TileGrid.visibleTiles(viewport)  → list of (col,row,lod)
   │  for each missing tile:
   ▼
RenderEngine::scaleRegion(srcRegion, lod)  → decoded tile (core/, no Qt paint)
   │  cached in TileCache (LRU, LOD-aware)
   ▼
ImageViewer paints only cached visible tiles via RawImageView (existing widget)
```
- **LOD**: pick a downsampled level per tile so a zoomed-out view decodes small
  tiles; zoom-in decodes higher-LOD tiles on demand.
- **Async**: missing-tile decode runs on the existing `TaskScheduler` (Thumbnail
  priority pool), UI paints whatever is cached; tiles pop in as they arrive
  (progressive).
- **Memory**: only visible tiles + a small LRU remain resident; full bitmap is
  never materialized in the widget.

## Acceptance (v1, CPU)
- [ ] Open a 12000×8000 JPEG: first paint shows within budget; **zoom does not
      block the UI** (main thread free).
- [ ] Only visible-region tiles are decoded (prove via `TileCache` hit/miss +
      a decode-count assert in `test_tilepipeline`).
- [ ] Memory stays bounded under continuous zoom/pan (no full-bitmap hold).
- [ ] `test_tilepipeline` added (reuses `TileCache`/`TileGrid`/`Viewport`).

## Non-goals (v1)
- ❌ GPU / OpenGL / Direct2D texture upload (explicitly deferred by review).
- ❌ Rewriting `RenderEngine` or `ImageFrame`.
- ❌ RAW decode (separate `TODO(M7): RAW`).

## Sequencing
This RFC is written now; **implementation starts only after** (a) product_workflow
acceptance approved, (b) Workspace persistence done, (c) real-dataset benchmark
green. Tile rendering is the largest remaining item — schedule it last in M11.
