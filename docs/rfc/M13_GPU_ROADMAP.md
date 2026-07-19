# RFC: M13 Phase 7 — GPU Render Route

- **Status:** DRAFT (design only — no implementation in this phase)
- **Date:** 2026-07-19
- **Owner:** Hermes (commander)
- **Related:** `docs/rfc/M13_PRODUCT_BETA.md` (Phase 7), `docs/rfc/M13_TILE_PIPELINE.md` (P2, landed), `core/render/{RenderEngine,TileCache,TileGrid,Viewport}.{h,cpp}`

## 1. Context

The current render path is **CPU tile scaling + QPainter blit**:

```
ImageViewer::paintEvent
  -> TileGrid.visibleTiles(viewport)        // which source tiles are on screen
  -> RenderEngine::scaleRegion(src, region, target, ...)   // CPU bilinear/area scale
  -> TileCache (LRU, keyed by imageId,col,row,lod)          // memory tier
  -> QPainter::drawImage(qrect, qimage)                     // software blit to widget
```

This is correct and already satisfies the M13 Phase 2 acceptance (open a
12 000×8 000 JPEG, only the visible region is processed, zoom does not block
the UI). It does **not** yet use the GPU for the scale or the blit.

The review (2026-07-19) explicitly said: *"next stage do NOT immediately do
GPU. First complete the Tile Pipeline RFC. CPU tile is enough for v1."* Phase 2
(Tile Pipeline) is **already landed** (`TileCache`/`TileGrid`/`Viewport` +
`test_tilecache`/`test_render_pipeline`, wired into `ImageViewer::paintEvent`).
So the prerequisite the review set is met; this RFC plans the *optional* GPU
step that follows, without committing to it this milestone.

## 2. Problem / Opportunity

For the currently-supported formats (JPEG/PNG/TIFF up to ~12K×8K), CPU tile
scaling is sufficient — the bench shows first-thumbnail 34 ms (cold) and
switch p50 4.5 ms. The GPU route is justified only when:

- **100+ MP images** (the review's stated target) become a first-class use case, OR
- **4K/8K displays at 1:1** with smooth pan/zoom is required, OR
- **many simultaneous viewers** (compare of large images) tax the CPU.

Until those are real product requirements, GPU rendering is a **cost** (driver
complexity, QA surface, fallback paths), not a benefit. This RFC therefore
recommends a **staged, reversible** route and a hard gate: do not start Stage B
until Stage A is measured to be insufficient on real 100 MP data.

## 3. Current architecture facts (so the route is grounded)

- `RenderEngine` is a **Qt-free** core class; `scaleRegion` returns an
  `ImageData` (CPU buffer). The Widget calls it and blits with `QPainter`.
- `TileGrid` enumerates visible tiles; `TileCache` is an LRU keyed by
  `(imageId, col, row, lod)`. Both are domain-free and unit-tested.
- `Viewport` owns pan/zoom math (domain-free).
- **No** OpenGL/D3D/Vulkan/Direct2D code exists today.
- Qt6 RHI (QRhi) supports D3D11 / Vulkan / Metal / OpenGL backends uniformly;
  `QQuickWidget`/`QOpenGLWidget` host a GPU surface. Qt6 on Windows defaults to
  D3D11 via QRhi.

## 4. Proposed route (staged)

### Stage A — GPU upload + QPainter-on-GPU blit (lowest risk)
- Host the viewer in a `QOpenGLWidget` (or switch `ImageViewer` to a
  `QQuickWidget` + `QSGTexture`). Upload each **cached tile** (`TileCache`
  entry) once to a GPU texture; subsequent paints `drawImage` blits via the GPU
  compositor instead of software raster.
- **What it buys:** the per-frame blit cost drops from CPU→GPU; pan/zoom of
  already-cached tiles becomes near-free. CPU `scaleRegion` still runs only for
  *missing* tiles (same as today).
- **Risk:** low. Qt handles the context; fallback to software if no GL.
- **Decision gate:** measure 100 MP pan/zoom fps with Stage A vs CPU-only on
  the target hardware. If ≥ 60 fps at 1:1 pan, **stop here** — do not proceed
  to B/C.

### Stage B — GPU scaling (the actual scale on GPU)
- Move `RenderEngine::scaleRegion` to a GPU path: upload the source tile
  texture, sample it with a GPU sampler at the target LOD (bilinear/trilinear),
  read back (or composite directly) the scaled tile into `TileCache`.
- `RenderEngine` keeps its `ImageData` CPU signature but gains a GPU backend
  selected at runtime; the `TileCache` key contract is unchanged, so this is
  invisible to `ImageViewer`.
- **What it buys:** missing-tile scaling (the only remaining CPU hot path)
  moves to GPU; large-image zoom becomes smooth.
- **Risk:** medium. Sampler quality vs CPU area-scale must match (visual diff
  test). Read-back cost if the cache needs CPU `ImageData`.

### Stage C — Direct2D / D3D11 direct compositing (Windows optimization)
- Bypass `QPainter` entirely for the viewer: composite tiles with Direct2D
  (D3D11) into a swap chain, host it in a native HWND child of the Qt window.
- **What it buys:** removes the Qt scene-graph overhead for the hot viewer;
  best-case Windows latency.
- **Risk:** high. Native/D3D interop with Qt event loop; breaks the
  platform-abstract `QWidget` boundary (AGENTS.md: UI layer = Qt Widgets only).
  **Requires an ADR + architect sign-off** — do NOT do this while the
  `UI = Qt Widgets` boundary is frozen.

### Stage D — Vulkan (future, not planned)
- Qt6 QRhi already abstracts Vulkan; once Stage B is proven on D3D11, the
  backend can flip to Vulkan with no app-code change. Track only.

## 5. Decision

**Recommendation:** land **Stage A only**, behind a runtime flag
(`MVIEWER_GPU_BLIT`, default OFF), and gate Stage B on a measured 100 MP
deficit. **Do NOT do Stage C/D in M13** — they violate the frozen UI boundary
and the review's "CPU tile is enough for v1" guidance.

Subtraction check (M13 rule): this RFC adds **no code**. If approved, Stage A
is a later milestone; it must (a) keep `RenderEngine` Qt-free (GPU backend
injected, not baked in), (b) preserve the `TileCache`/`TileGrid`/`Viewport`
contracts, (c) keep a software fallback so headless/CI still works.

## 6. Acceptance for THIS phase (Phase 7)

- [x] RFC written and grounded in the actual render path.
- [x] Staged route with decision gates and a stop condition.
- [x] UI-boundary / freeze risk called out explicitly (Stage C deferred).
- [ ] RFC approved by commander (no implementation until then).

## 7. Out of scope (explicit)

- Any `RenderEngine` GPU backend code.
- Any `QOpenGLWidget` / `QQuickWidget` migration.
- Any D3D11/Vulkan context creation.
- Changing the `UI = Qt Widgets` boundary (frozen per AGENTS.md).
