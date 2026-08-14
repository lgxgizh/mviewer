# M38 ImageViewer Render Convergence Review

Date: 2026-08-14  
Repository: `D:\mviewer`  
Canonical build entry point: `powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test`

## Scope

M38 closes the single-image Browse → fullscreen Viewer → Fit → tile upgrade
path. It does not add a new product category, GPU backend, QML surface,
analysis pixel contract, or second build path.

## Before state

- `ImageViewer::paintEvent()` called the synchronous `TileCache::request()`
  path. A cache miss performed `RenderEngine::scaleRegion()` and ICC display
  materialization from the GUI paint call stack.
- Tile identity was image + column + row + LOD even though the payload size
  changed with transient zoom and HiDPI policy.
- TileCache retained a count-limited LRU without accounting for payload bytes.
- The Viewer did not consume the already-ready gallery thumbnail as its first
  presentation, so cold FullImage decode could expose a blank first frame.
- Copy Color assumed RGB byte order; Copy Image and Save As used analysis-domain
  pixels instead of the shared display materialization contract.

## Implementation

1. `AsyncTileRequestManager` owns Missing → Pending → Ready coordination over
   the existing DecodePool. Duplicate canonical keys are coalesced; reset of
   an image/view generation soft-cancels obsolete requests and drops late
   results. Workers only create `ImageData`; UI delivery performs repaint,
   upload and GL work.
2. `TileKey` now includes `renderScalePercent`. LOD payload dimensions are
   canonical (`sourceExtent / 2^LOD × renderScale`) and compositor zoom is
   separate, so continuous zoom cannot churn or alias a key. CPU and GPU paths
   consume the same display-ready tile payload.
3. TileCache enforces `maxBytes`, tracks byte usage, and retains optional
   explicit `maxTiles` compatibility. Oversized tiles are not retained;
   `clear()` returns usage to zero and metrics expose count/bytes/hit/miss/
   eviction values.
4. Browse supplies a warm thumbnail to the Viewer as a provisional display.
   It remains display-only until the FullImage frame arrives and is removed
   after the visible canonical tiles are ready. Analysis, ROI, histogram and
   Pixel Inspector remain FullImage-only.
5. Fullscreen uses `FitPolicy::MaximizeClient`; normal windows use the named
   `Comfortable` policy. Copy Color uses `samplePixel()`, and Copy/Save use
   `toDisplayQImage()` without mutating analysis bytes. Overlay results use a
   separate bounded derived cache.

## Tests added or extended

- `render_pipeline_tests`: max-fit geometry for landscape, portrait, square,
  ultra-wide and ultra-tall sources.
- `tilecache_tests`: byte budget/eviction/oversized policy, canonical zoom
  identity, DPR separation, non-blocking request timing, Pending de-duplication,
  generation cancellation and convergence.
- Existing `m36_display_tests`, `m27_lifetime_tests`, Browse convergence and
  workflow suites remain regression gates.

## Verification

Focused results from this workspace:

| Gate | Result |
| --- | --- |
| `render_pipeline_tests` | PASS |
| `tilecache_tests` | PASS |
| `m36_display_tests` | PASS |
| `m27_thumbnail_tests` | PASS (60.39 s) |
| `mviewer_bench --scenarios B10 --enforce` | PASS (canonical target-resolution fixture) |
| Debug build via `build.ps1 Debug` | completed with existing C4819/C4834 warnings |

The pre-fix canonical `build.ps1 Test` run completed with 88 registered tests:
81 passed and 7 failed. Its failures were `workflow_ux_tests`,
`analyze_acceptance_tests`, `appstate_tests`, `cache_tests`,
`repository_tests`, `browse_convergence_ui_tests` and `bench_enforce`. After
correcting the B10 synthetic decoder to honor canonical tile output
dimensions, the isolated B10 gate passed. A post-fix canonical rerun reached
the 10-minute command bound before CTest emitted a per-test result; its
temporary CTest log contained only the start marker. The remaining full-gate
evidence therefore still requires a Windows environment with permitted temp,
AppConfig and cache writes.

## Threading and lifetime contract

- GUI thread: viewport math, Ready lookup, provisional compositing, overlays,
  QPainter, QPixmap/texture upload and QWidget/GL access.
- DecodePool: source-region scaling, canonical tile materialization and ICC
  conversion over copied/value-owned ImageData.
- Late worker completion: generation and cancellation checks precede cache
  insertion or UI callback; manager pending state converges to zero after
  cancellation/destruction.

## Open evidence

- Real-display HiDPI/fullscreen visual feel and opt-in GPU runtime capture still
  require a hardware UX pass.
- The canonical gate still needs a clean environment rerun for the
  baseline-sensitive suites above. No claim of full M38 completion is made
  until that run is green or its environment blocker is explicitly accepted
  by the project owner.

## Verdict

Implementation is in place, focused core/display gates are green, and the
isolated performance fixture is green. Final M38 verdict remains **pending
canonical Windows gate and UX review**.
