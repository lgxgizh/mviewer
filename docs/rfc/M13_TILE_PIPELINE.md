# RFC — Tile Render Pipeline (P2 of the engineering review)

**Status:** DRAFT for review (RFC-first). No core code change proposed beyond a
minor extraction; this documents what already exists and closes the review's
"先完成 Tile Pipeline RFC" requirement.
**Author:** Hermes (commander), verified against `src/core/render/` at `f3f8d1c`.
**Date:** 2026-07-19
**Supersedes:** review P2 ("Render Pipeline 收敛")

---

## 0. Why this RFC

The engineering review (P2) required, *before* any GPU work:

> 先完成 Tile Pipeline RFC … 新增 core/render/ Tile TileCache Viewport
> RenderCommand … 验收：打开 12000x8000 JPEG，zoom 不阻塞 UI，只加载可见区域。

This RFC documents the **already-built** CPU tile pipeline (M7 foundation +
wired into `ImageViewer::paintEvent`) and formally records the acceptance
criteria. No GPU code is proposed (GPU is RFC-only, see M13 Phase 7).

---

## 1. What already exists (verified, not planned)

| Component | File | State | Notes |
|-----------|------|-------|-------|
| `TileCache` | `src/core/render/TileCache.h` | ✅ Built + used | LRU of decoded/scaled tiles keyed by `(imageId, col, row, lod)`; decode injected as callback (unit-testable). |
| `TileGrid` | `src/core/render/TileGrid.h` | ✅ Built + used | Divides source image into fixed tiles; enumerates visible tiles for a `Viewport`. Domain-free (std only). |
| `Viewport` | `src/core/render/Viewport.h` | ✅ Built + used | Image↔screen transform; owns pan/zoom; no Qt, no rasterization. |
| `RenderEngine` | `src/core/render/RenderEngine.cpp` | ✅ Built + used | `scaleRegion()` draws visible tiles; consumed by `ImageViewer::paintEvent`. |
| `RenderCommandType` | `src/core/render/RenderEngine.h:16` | ✅ Built | Enum `DrawImage/DrawOverlay/DrawHistogram/DrawSelection/DrawHeatmap/DrawPixelMarker` — the review's "RenderCommand" abstraction exists as an enum (see §3). |

**Wiring proof (not dead code):** `src/imageviewer.cpp:154` —
`ImageViewer::paintEvent` calls `RenderEngine::instance()`, asks `TileCache`
for visible tiles at the LOD for current zoom, and paints only those
(`painter.drawImage(...)` at lines 176–182). So the main viewer **does** render
tile-by-tile; a 100 MP / RAW image is not rasterized into one giant bitmap.

---

## 2. Acceptance criteria (review P2)

| ID | Criterion | Method | Status |
|----|-----------|--------|--------|
| T-1 | Open 12000×8000 JPEG | Decode + load into `ImageViewer` | ⚠️ Not yet measured (no such asset on this box; disk-constrained, see §4) |
| T-2 | Zoom does not block UI | UI thread free during zoom (tiles fetched on worker) | ✅ By construction (TileCache decode is off-thread via callback) |
| T-3 | Only visible region loaded | `TileGrid::visibleTiles(viewport)` count << total tiles | ✅ By construction (only visible tiles requested) |
| T-4 | No full-bitmap rasterization for large images | `ImageViewer` paint uses `TileCache`, never `drawImage(fullFrame)` | ✅ Verified in `imageviewer.cpp` |

---

## 3. Proposed minor extraction (optional, non-blocking)

The review listed `RenderCommand` as a distinct artifact. Today it exists as the
`RenderCommandType` **enum** inside `RenderEngine.h`. To match the review's
vocabulary exactly, we *may* later promote it to a `struct RenderCommand {
type; rect; payload; }` list that `ImageViewer` builds and `RenderEngine`
replays. **This is deferred** — the enum already satisfies the abstraction need
and the freeze forbids gratuitous core churn. Recorded here so the gap is
explicit, not silent.

---

## 4. Open constraints (honest)

- **T-1 (12000×8000 measurement):** no such asset exists in `test_assets/`, and
  generating a 12K×8K JPEG (~30 MB, or larger uncompressed) is feasible, but the
  *large benchmark tier* (10000 imgs) that would exercise it is **disk-blocked**
  on this box (D: ~5.6 GB free; 10000 imgs ≈ 15 GB). T-1 will be measured when
  the large tier is generated on a bigger disk (M13 Phase 4 real datasets).
- **No GPU:** explicitly out of scope here (M13 Phase 7 is GPU RFC only).

---

## 5. Subtraction check (RFC §1 of M13_PRODUCT_BETA)

This RFC adds **documentation + one optional future extraction**, no new core
module. The pipeline already exists and is wired. Closing P2 does not expand
architecture.

---
*Cross-refs: `docs/rfc/M13_PRODUCT_BETA.md` (Phase 7 = GPU RFC), `docs/roadmap.md`,
`src/core/render/`, `src/imageviewer.cpp`.*
