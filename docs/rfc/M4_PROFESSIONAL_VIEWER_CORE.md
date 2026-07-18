# RFC: M4 — Professional Viewer Core (Frozen Design + Maturity Scope)

**Status:** RFC (for review) — design frozen from existing implementation;
maturity work scoped, not yet started.
**Date:** 2026-07-17
**Depends on:** M3 (frozen, `docs/milestones/M3.md`), RFC-006 (Compare
Controllers), ADR-009 (Split Compare Engine), ADR-011 (Viewer Core boundary).

---

## 0. Context: what already exists

This RFC does **not** invent the Compare Engine. The review that prompted it
assumed M3 was "just starting" and the Compare Engine was from-scratch P1 work.
That assumption is stale: the codebase is already at M8. The following are
**already implemented and on disk** (verified 2026-07-17):

- **Compare Engine** (`src/core/compare/`): `CompareEngine` (pure facade) +
  `SyncController` (shared zoom/pan/scroll + per-cell independent transform),
  `BlinkController`, `DifferenceEngine` (`differenceMap`), `SelectionController`,
  `ViewportController`, `PixelController` (fifth module: `inspectPixel` →
  samples + deltas vs base). `domain/CompareSession.h` owns all comparison
  state; Workspace only renders.
- **AnalyzerRegistry** (`src/core/analyzer/`): `Analyzer` base (unified
  interface — `analyze(frame)` / `analyzeRegion(frame, Selection)`), and
  **8 built-in analyzers**: Histogram, Noise, Entropy, PSNR, RGBMean,
  Sharpness, SSIM + base. Not placeholders.
- **Selection**: `domain/Selection.h` is a domain ROI object (x/y/width/height,
  `contains`, `area`); analyzers consume it, never `QRect`.
- **Command system** (`src/core/command/`): `ICommand`, `CommandStack`
  (Undo/Redo), `CommandRegistry`, and `CropCommand`, `CompareCommand`,
  `RotateCommand`, `OpenDirectoryCommand`, `LabelCommand`, `RenameCommand`,
  `DeleteCommand`, `ToggleHistogramCommand`.

Therefore this RFC's job is to **(a) freeze the existing Compare Engine design
as the accepted M4 architecture**, and **(b) define the maturity work that turns
"working Compare Engine" into "professional viewer core."** It does not expand
scope into Render Tile Pipeline, Plugin ABI, or Perfetto (see ADR-011).

---

## 1. Goal

From "can load and compare images" → "usable as a professional image-viewer
core for ISP / Camera / CV engineers." Maturity, hardening, and polish — not new
engine architecture.

## 2. Architecture (frozen — from existing code)

```
CompareWorkspace (UI QWidget; renders only; owns CompareEngine BY VALUE)
        │ reads snapshot
        ▼
CompareSession (domain: SERIALIZATION SNAPSHOT of compare state, not live owner)
        ▲  (controllers below own the live, mutable state)
CompareEngine (facade, pure — no Qt paint; owns frames as shared_ptr)
   ├── SyncController      shared zoom/pan/scroll + per-cell transform
   ├── BlinkController     alternating A/B highlight (configurable interval)
   ├── DifferenceEngine    abs(A-B) diff map + heatmap + PSNR/SSIM (async via JobSystem)
   ├── SelectionController ROI sync across cells
   ├── ViewportController  grid (cols/rows) + per-cell size
   └── PixelController     inspectPixel(x,y) → samples + delta vs base

AnalyzerRegistry  ──► Analyzer (unified IAnalyzer)
   Histogram / Noise / Entropy / PSNR / RGBMean / Sharpness / SSIM
   analyze(frame) | analyzeRegion(frame, Selection)
```

> **Lifecycle (verify before changing):** `CompareWorkspace` (a `QWidget`)
> owns `CompareEngine` **by value** (`CompareEngine m_engine;`) — not a raw
> pointer into a Widget, not dangling. `CompareEngine` owns frames as
> `shared_ptr<ImageFrame>`. `CompareSession` returned by `session()` is a
> **by-value copy / snapshot**; the live mutable state lives in the controllers
> (Sync/Selection/Viewport). Do NOT "fix" this by making `CompareSession` the
> live owner — that would couple domain to engine state (violates ADR-003/009).

### Class relationships (as implemented)

- `CompareEngine` owns the `std::vector<shared_ptr<ImageFrame>>` and composes
  the 6 controllers. UI never owns comparison logic (ADR-009).
- `CompareSession` is a **serialization snapshot** of compare state (returned by
  `session()` by value); the live mutable state lives in the controllers
  (Sync/Selection/Viewport). Workspace reads the snapshot; it never owns logic.
- `Analyzer` is the plugin interface; `AnalyzerRegistry::instance()` registers
  built-ins idempotently (static-lib object-file pruning safe).

### Data flow (dual-image)

1. `setImages({A,B})` → `rebuildLayout()` (2 cols × 1 row).
2. `zoomAt(vx,vy,factor)` → `SyncController` updates `SyncTransform`; both cells
   read the shared transform when `syncEnabled()`.
3. `inspectPixel(px,py)` → `PixelController` reads `ImageFrame` pixels from each
   cell, returns `ProbeResult` { per-cell RGB, base RGB, delta }.
4. `differenceMap(i, base)` → submitted to `JobSystem`/`TaskScheduler`
  (Analysis/Background pool); `DifferenceEngine` computes abs(A-B) off the UI
  thread; result delivered via `EventBus` (no synchronous UI-thread compute).

### Thread model

- Decode/off-thread work runs on `TaskScheduler` pools (Decode/Thumbnail/UI).
- Compare transform math is single-threaded on the UI thread (cheap; no decode).
- `PixelController` reads already-decoded `ImageFrame` pixels synchronously
  (no allocation, no decode) — safe because frames are immutable shared_ptr.

## 3. Maturity scope (the actual M4 work to do)

Priority order per review:

1. **Compare Engine synchronization hardening (P0)**
   - Dual-image A|B: synchronized zoom (both 200%), synchronized pan offset
     (x,y identical), blink @500 ms (A→B→A→B), diff output `abs(A-B)`.
   - Extend to 2–8 images / 50 MP inputs without UI stall (roadmap M4 bar).
2. **Pixel Inspector promotion (P0/P1)**
   - Real-time x,y + RGB + YUV + delta under cursor; panel subscribes to
     `pixelInfo`, not polled.
3. **Selection system (P1)**
   - Current `Selection` is a rectangle. Add `RectangleROI` / `PolygonROI`
     domain types; analyzers, diff, crop all consume `ROI` (not `QRect`).
   - NOTE: `RectangleROI`/`PolygonROI` do **not** yet exist in code — this is
     M4 *work*, not a completed item.

4. **Diff must be async (P0 — blocking-gap fix)**
   - `CompareEngine::differenceMap` currently calls `DifferenceEngine::
     differenceMap` **synchronously on the caller's thread** — if invoked from
     the UI thread this blocks on large images. M4 MUST route diff through
     `JobSystem` / `TaskScheduler` (Analysis or Background pool); the UI receives
     a `DiffResult` via `EventBus`. No synchronous diff on the UI thread, ever.
   - `BlinkController` is already timer-driven (off the compute path) — keep.
   - `EventBus` (`src/core/EventBus.*`) is the sanctioned transport for
     sync-changed / pixel-changed / diff-ready notifications; name it explicitly
     (the inspector panel subscribes to `pixelInfo` through it).
5. **AnalyzerRegistry interface stability (P1)**
   - Freeze `Analyzer::analyze` / `analyzeRegion` signatures; no new algorithms
     in M4 — stabilize the contract so plugins can rely on it.
6. **Command system unification (P1)**
   - Ensure `OpenImageCommand`, `CompareCommand`, `CropCommand`, `RotateCommand`
     all go through `CommandStack` so Undo/Redo/Replay/AI-Agent all share one
     path. (Most already do; verify coverage.)

## 4. Out of scope (explicit — ADR-011)

- ❌ Render Tile Pipeline (deferred to post-M8; `core/render/Viewport` +
  `TileGrid` foundation already seeded in M7 — extend there later).
- ❌ Plugin ABI changes (current `example_analyzer` E2E plugin is the contract;
  freeze it).
- ❌ Perfetto (opt-in trace shim exists in M7; not a dependency of M4).

## 5. Test strategy

- Extend `src/core/test_compare.cpp` (currently 139 lines: layout + sync +
  diff + pixel probe checks). Add:
  - 8-image grid sync (no stall on 50 MP).
  - Blink index alternation timing.
  - `Selection`-driven analyzer region == full-image analysis on same region.
  - Pixel probe delta correctness (A vs B identical region → 0 delta).
- Keep `test_m3acceptance` green (regression guard for the core pipeline).

## 6. Acceptance criteria (M4)

- [ ] Dual image A|B: synchronized zoom (both 200%), synchronized pan offset
      (x,y consistent), diff `abs(A-B)`, blink @500 ms A↔B.
- [ ] Diff of two 50 MP images is computed **off the UI thread** via `JobSystem`/
      `TaskScheduler`; result delivered through `EventBus` — UI never blocks on
      diff compute. (Blocking-gap fix; was synchronous before M4.)
- [ ] Pixel Inspector shows x,y + RGB + YUV + delta in real time.
- [ ] `Selection` (ROI) is the sole ROI type passed to analyzers/diff/crop; no
      `QRect` crosses the core API.
- [ ] Every built-in analyzer reachable through `AnalyzerRegistry`; region
      analysis matches full-image analysis on the same region.
- [ ] `OpenImageCommand`/`CompareCommand`/`CropCommand`/`RotateCommand` all route
      through `CommandStack` (Undo/Redo works across them).
- [ ] `test_compare` + `test_m3acceptance` both green.

## 7. Related

- RFC-006 (Compare Engine Controllers) — `docs/rfc/006-compare-controllers.md`
- ADR-009 (Why split Compare Engine) — `docs/adr/009-why-split-compare-engine.md`
- ADR-011 (Viewer Core boundary) — `docs/adr/011-viewer-core-boundary.md`
- M3 frozen baseline — `docs/milestones/M3.md`
