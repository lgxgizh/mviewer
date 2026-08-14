# M39 Real-world interaction reliability & export convergence

Snapshot: 2026-08-14

## Scope

M39 closes the reliability gaps between the real viewer interaction path and
the export/report path. The existing UI → application → core → domain
boundaries and build entry points remain unchanged.

## Implemented

- `AsyncTileRequestManager` retains scheduler-rejected requests and retries
  them with bounded backoff. Visible requests are de-duplicated and pending
  work is capped; reset/destruction cancels handles safely.
- Image lifetime and viewport revisions are separate. Image replacement resets
  tile/overlay generations; pan, zoom, resize and fit advance only the viewport
  revision.
- `ImageViewer::paintEvent()` draws `ImageData` through a non-owning QImage
  view and keeps derived overlay materialization on the Decode pool.
- ROI statistics use direct clipped reads over the shared frame buffer. A
  latest-wins Analysis task delivers only when image, selection and revision
  guards still match.
- Convert, Contact Sheet, PDF, CSV, JSON, HTML and Clipboard use one
  cancellable asynchronous `ExportJob` path. Reports are escaped and written
  atomically; non-finite JSON numbers become `null`.
- Legacy directory-batch enumeration moved from `ExportDialog` to the worker;
  the UI no longer calls `QDir::entryList()` for export sources.
- Fullscreen requested state is cleared before exit and restored after the
  native transition, preventing sticky F/F11 state on the next open.
- Added scheduler rejection, ROI, report-parser, export-mode, source-directory
  and lifecycle coverage. The workflow export test waits for asynchronous
  report output.

## Verification

- `powershell -ExecutionPolicy Bypass -File .\build.ps1 Release`: passed.
- Focused tile-cache test: 47 passed, 0 failed.
- M27 lifetime test: 30 passed, 0 failed.
- M27 lifecycle torture, 100 rounds: passed with stable RSS/handle/thread
  counts.
- Export report, ExportJob and export acceptance suites passed; export
  acceptance covers Windows path collisions, Unicode paths, atomic replacement,
  cancellation and degraded directories.
- Default local `.\build.ps1 Test`: 81/88 passed. The seven failures are the
  managed-sandbox AppConfig/cache/repository/temp write restrictions.
- With TEMP, APPDATA and LOCALAPPDATA redirected to an explicit workspace-owned
  runtime directory: 87/88 passed, including `bench_smoke` and `bench_enforce`.
  The remaining workflow assertion is the Windows-native QSettings
  sidebar-migration observation, which is not readable back in this restricted
  desktop profile.

## Remaining gate note

No commit is made from this environment because the canonical default gate is
not fully green. A normal desktop profile with writable native settings and
cache locations should be used for the final 88/88 confirmation.
