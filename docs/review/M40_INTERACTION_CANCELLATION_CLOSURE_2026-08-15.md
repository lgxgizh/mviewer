# M40 — Interaction Cancellation & UI-Thread Purity Closure

Date: 2026-08-15
Baseline commit: `5d148bf` (`fix bug`)
Scope: Compare loading, ImageViewer fullscreen/copy/save, unified export
cancellation, retry ownership, and Contact/PDF staging memory.

## Baseline

The baseline was checked before the M40 edits. `build.ps1 Release` configured
and built successfully. The first `build.ps1 Test` run reached the CTest
matrix before its 120-second command limit; the observed failures were the
existing repository/cache disk-tier assertions (four failures in
`repository_tests`). This is recorded as an environment-sensitive baseline,
not as evidence that the full baseline gate was green.

## Closure changes

- Compare now uses `ImageRepository::loadAsyncCancellable`. Each request has
  one atomic accounting bit, owned request handles, generation-scoped result
  adoption, cancellation of queued work, and destructor cleanup. A cancelled
  request is accounted locally because repository cancellation intentionally
  suppresses its callback.
- Compare worker callbacks only update shared batch state. `finishLoad()` is
  queued to the GUI thread and is guarded by both `QPointer` lifetime and the
  load generation.
- `ImageViewer::setFullscreenRequested()` is the authoritative fullscreen
  API/property boundary. Keyboard, Browse and slideshow entry points use it;
  fit policy reads the requested property rather than native state.
- Copy and Save As dispatch a worker-side `ExportJob`. Full decode,
  display/ICC conversion and encoding stay out of QAction/QWidget callbacks;
  only the final clipboard assignment is performed on the GUI thread.
- `ExportDialog` Convert, Clipboard, Contact Sheet, PDF, CSV, JSON and HTML
  all use one cancellable async runner. Cancel, reject, close and destruction
  invalidate the generation and suppress stale progress/results.
- `AsyncTileRequestManager` owns one stoppable `std::jthread` retry loop.
  Reset and destruction wake and join it immediately; handle moves and
  cancellation are synchronized with submission, and no detached retry thread
  remains.
- Contact/PDF staging enforces a 512 MiB default hard budget. Explicit Save As
  destinations and display-preserving worker decode are part of the public
  `ExportJobConfig` contract.

## Focused evidence

After a clean Release rebuild (which also rebuilt every Compare split TU), the
focused M40 set passed in one process:

| Test | Result |
| --- | --- |
| `compare_session_tests` | PASS |
| `compare_acceptance_tests` | PASS, including the M40 A→B cancellation/rejection cases |
| `tilecache_tests` | PASS, including retry reset/destruction cases |
| `export_job_tests` | PASS, including Save As, Clipboard, pre-cancel and budget cases |

The clean rebuild was important: an earlier incremental build retained stale
Compare split-TU objects after the header layout changed, producing a false
constructor crash. The final verification must therefore use the clean-built
tree.

## Verification status

The final clean-build `build.ps1 Test` completed with CTest exit 8: **82/88
passed**. The M40-focused tests and `compare_acceptance_tests` passed. The
remaining failures were:

- `workflow_ux_tests`: 14 workflow persistence/UX assertions.
- `analyze_acceptance_tests`: five analysis-history persistence assertions.
- `appstate_tests`: six AppState persistence assertions.
- `cache_tests`, `repository_tests`, `browse_convergence_ui_tests`: disk/cache
  writes and cache reads unavailable in the managed runtime.

The gate is therefore not green yet. These failures are retained as explicit
follow-up evidence rather than being reported as a completed full-gate pass.

## Review checklist

- No `QImage(currentImagePath())` remains in the MainWindow copy path.
- No detached retry thread remains in `AsyncTileRequestManager`.
- Compare completion is exactly-once per request and stale batches cannot adopt
  frames.
- Export progress/result callbacks are generation- and lifetime-guarded.
- Build system, presets and CI files were not changed.
