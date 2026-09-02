# M61 Phase 0 — ROI workflow baseline

Date: 2026-09-02

Baseline: `4a016ed` (`master`, clean and aligned with `origin/master`)

Milestone: M61 Professional Linked ROI Measurement Workflow Convergence

## Review scope

The baseline review covered the existing Compare ROI, navigation, interaction,
canvas rendering/materialization, `RawImageView`, core statistics, source-backed
decode provider, Qt decoder, M60 focused tests, Compare acceptance/workflow UX
tests, and the M47/M53/M59/M60 RFC and closure evidence.

## Current truth before M61 product changes

| Area | Baseline behavior | M61 gap / RED expectation |
| --- | --- | --- |
| Canonical geometry | One half-open `mviewer::domain::Selection` in displayed source pixels | Keep this as the only ROI model |
| Grid creation | Real right press/move/release creates a clipped ROI | Preserve |
| Grid preview | `selectionPreviewChanged` mirrors cheap geometry to equal-sized panes | Preserve and extend to editing |
| Grid final measurement | Release queues one Analysis task and generation guards delivery | Add explicit lifecycle state and row-level cancellation |
| Canvas modes | Split/Swipe/Overlay/Checkerboard hide `RawImageView`; canvas handles wheel and left-drag only | ROI is neither painted nor right-drag editable in any canvas mode |
| Existing ROI editing | A new right drag always replaces the ROI | Add move plus four-edge/four-corner resize |
| Pointer cost | Preview does not schedule ROI statistics | Preserve; annotation repaint must not rebuild base surfaces |
| Cancellation | Worker checks cancellation once per pane | A large single-pane scan has no row/chunk checkpoints |
| Backpressure | A rejected `submit()` leaves a null handle while status still reads active | Surface terminal `Backpressured` state |
| Stale delivery | Generation, pane count, geometry, linked state are checked | Preserve and cover A→B→A, clear/navigation/destruction |
| Source-backed measurement | `FullDecodeCrop` is rejected; every capability decoder with `canNativeRegion()==false` is classified `BoundedRasterRegion` | Classification is too broad; only evidence-backed region paths may be source measurement |
| JPEG | Qt JPEG reduced-DCT LOD is proven; clip-region output is 8-bit | Region behavior must be declared explicitly and instrumented |
| TIFF | Unrotated Windows TIFF uses WIC clip/scale with bounded output; high-bit data is normalized to 8-bit | May be measured at the honest current 8-bit boundary |
| PNG/BMP/other Qt formats | Qt `ClipRect` can require full-raster work; no bounded exact-source proof exists | Source-backed ROI must report `Unsupported`, not use display LOD or invented RGB |
| Precision | Authoritative `ImageData` is 8-bit | UI must say `Source RGB · 8-bit analysis`; no 16-bit claim |
| Side panel hidden | Measurement table still updates but is invisible | Add compact non-modal viewport HUD |
| Table | Seven columns; no copy action, reason column, filename tooltip contract, or numeric alignment | Professionalize and add TSV copy |
| Tests | M60 tests pure geometry/statistics; acceptance only persists programmatic ROI | Add real right-button Grid/Canvas/editing/reliability tests |

## RED gates established for M61

`m61_roi_workflow_tests` is added before product changes. Its initial baseline
must fail while exercising the production widgets with real mouse events:

- Grid right-button press → move → release, live peer preview, and final table.
- Split, Overlay, Swipe, and Checkerboard right-drag creation on the real canvas.
- Existing ROI move and corner resize rather than replacement-only drawing.
- Canonical ROI preservation across Canvas/Grid transitions.
- Analysis-pool pause/rejection produces a visible `Backpressured` terminal state.
- Side-panel-hidden HUD and clipboard action exist.
- Unequal dimensions remain unlinked and do not publish a measurement.

Pure focused tests will additionally pin reverse/outside/1×1/all-handle
geometry, cancellable row scanning, source region classification, rapid
latest-wins behavior, and annotation-only repaint instrumentation.

No existing assertion is removed or weakened. Build scripts, presets,
Scheduler architecture, DecoderRegistry, and CI remain unchanged.
