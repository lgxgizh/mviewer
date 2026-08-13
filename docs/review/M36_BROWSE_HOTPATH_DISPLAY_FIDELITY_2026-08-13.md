# M36 Browse Hot Path & Display Fidelity Evidence

Date: 2026-08-13  
Repository: `D:\mviewer`  
Build entry point: `powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1`

## Baseline

The repository currently registers 88 CTest tests (`ctest -N`). The canonical
baseline was started with `build.ps1 Test`; Release compilation succeeded and
the run reached the workflow and benchmark stages. The complete run cannot be
called green in this restricted desktop environment: existing workflow/cache
tests fail when their temp/AppConfig/thumbnail-cache writes are rejected. The
failures are write-path failures such as `ThumbnailCache` put/get and recovery
fixture creation, not assertion failures in the new M36 display contract.

Focused M36/regression result after the changes:

| Test | Result |
| --- | --- |
| `compare_session_tests` | PASS |
| `async_lifetime_tests` | PASS |
| `compare_acceptance_tests` | PASS |
| `m36_display_tests` | PASS |
| `m27_lifetime_tests` | PASS |
| `ratingstore_tests` | PASS |
| `test_iccprofile` | PASS in the earlier focused run |
| `browse_convergence_ui_tests` | blocked by restricted cache/temp writes |
| `workflow_ux_tests` | blocked by restricted recovery/AppConfig writes; product assertions otherwise continued |

`build.ps1 Release` passed after the final implementation changes. Line counts
are `mainwindow.cpp` 740, `compareworkspace.cpp` 749 and
`thumbnailpanel.cpp` 726, within ADR-014 limits.

## Browse hot path

Original synchronous fan-out identified in `MainWindow::onCurrentImageChanged`:

`SelectionModel::setCurrentImage` → `onCurrentImageChanged` → thumbnail focus,
Preview, Viewer, `MetadataOverlay::setImage`/content build,
`MetadataReader::read`, `parseRawMetadata`, metadata panel, status metadata,
`DirectoryTree::navigateTo`, `RatingStore::addRecent`/flags persistence.

M36 behavior is now:

- selection handler derives filename/parent path from the selection identity;
  it does not construct `QFileInfo`, decode pixels, parse metadata, or write
  the recents file synchronously;
- warm thumbnails and gallery-known dimensions/file size are reused directly;
- Preview and Viewer remain scheduler-owned asynchronous work;
- MetadataOverlay, MetadataPanel and status use
  `MetadataPresentationService`, with path/generation latest-wins guards;
- hidden metadata consumers only record identity and cancel active work;
- DirectoryTree short-circuits an already-current directory before model or
  filesystem validation;
- `RatingStore::addRecent` updates memory immediately and coalesces persistence
  on a worker, flushing during shutdown/path changes.

`MetadataIndexer::cached()` is now a true memory-only snapshot lookup. File
identity validation occurs on the update/background path and never while its
global metadata mutex is held.

## Display color contract

`ImageFrame::pixels()` stays in the analysis domain. `toDisplayQImage()` and
`toDisplayImageData()` create display-only sRGB materializations. Thumbnail
workers convert before square-fit and persist display-ready schema-3 PNGs.
Preview scaled decoders return source metadata in the same reader pass. Viewer
TileCache stores display-ready tiles, so CPU painting and GPU upload consume the
same bytes and repaint does not repeat ICC conversion.

`m36_display_tests` covers sRGB, AdobeRGB, Display-P3 and no-profile samples;
it compares ImageData materialization to the QImage display reference and
checks that the analysis buffer is byte-identical afterward.

## Compare and Batch

Compare now uses one-host Option A. Every queued load captures its own dialog
and workspace; old-host destruction cannot clear a newer host. Session and
preset persistence retain all four sync states. Sync toggles only change future
propagation and do not call Fit/reset viewport.

Batch Analysis now submits a background task, processes one frame at a time,
retains only small `AnalyzerResult` values, reports progress, supports cancel,
guards panel lifetime, writes deterministic input-order output and reports
write/scheduler failure without blocking the UI.

## Remaining evidence gap

The full Definition of Done is not declared complete until the canonical test
run passes in an environment that permits the existing temp/AppConfig and
thumbnail-cache fixtures to create and write files. GPU parity was validated
through the shared pure display-ready tile contract; a host with a real GPU is
still required for an `MVIEWER_GPU=1` runtime capture.
