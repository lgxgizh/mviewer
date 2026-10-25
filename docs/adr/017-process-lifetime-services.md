# ADR 017 — Process-lifetime services and Qt teardown

## Context

The repository contains a family of "do not tear this down" workarounds that were
accumulated one bug at a time and had no single written rationale. A 2026-09
review flagged them as a HIGH-severity risk ("MainWindow teardown is not proven")
because a reader cannot tell an intentional platform workaround from forgotten
debt:

- `src/test_mainwindow_commandstack.cpp`, `src/test_rotate_selection.cpp` and
  `src/test_fullscreen_analysis.cpp` used to end with `terminateTestProcess()`
  (`TerminateProcess` / `std::_Exit`) so the stack `MainWindow` and
  `QApplication` were never destroyed. `src/test_m57_multiframe.cpp` used
  `std::_Exit`. The 2026-10-20 addendum retires those process-kill exits.
- `src/test_workflow_ux.cpp` / `src/test_workflow_ux_cases.inc` leave one
  window to process teardown: an offscreen `QFileDialog` leaves internal modal
  widgets that deadlock `MainWindow` destruction. That remains the single
  named exception.
- `src/core/plugin/PluginManager.cpp` unregisters a plugin but never
  `FreeLibrary`/`dlclose`s it: unloading a Qt-linking DLL at runtime (or during
  teardown) crashes the process through DLL-detach / CRT static ordering.
- Process-wide services such as `TaskScheduler`, `MetadataPresentationService`
  and the plugin handle table are leaked with `new`, while many other
  `instance()` accessors use function-local statics that *are* destroyed at
  exit. `TaskScheduler`'s pools must not be joined after `QCoreApplication` is
  gone, so the leaked ones are deliberate; the mixture was never stated.

What is genuinely untested is the part a test *can* control: whether real
objects (MainWindow with its browse/thumbnail/metadata pipelines,
CompareWorkspace with queued decode/diff batches) can be created, used and
destroyed repeatedly, including destruction while work is in flight.

## Decision

1. **Process-lifetime services are intentionally leaked.** A service that owns
   threads, Qt objects or native handles that must outlive the event loop is
   allocated with `new` and never destroyed; the OS reclaims it. Do not
   "fix" this into a static or a `unique_ptr` without proving that destruction
   is safe after `QCoreApplication` has been destroyed.
2. **Objects the application owns are destroyed normally.** `MainWindow`,
   `CompareWorkspace`, panels and viewers are stack- or owner-scoped and must
   survive destruction with queued work; async deliveries are guarded by
   lifetime tokens, `QPointer` and generation counters. `teardown_stress_tests`
   (added with this ADR, part of `.\build.ps1 Test`) enforces this over repeated
   rounds: each round must finish inside its budget, no top-level widget may
   survive it, and the application must still open a window afterwards.
3. **Process-exit workarounds stay, but only for process-exit reasons.** A test
   may skip a destructor or terminate its own process when the failure mode is
   Qt's *global* teardown (static destruction ordering, DLL detach, offscreen
   dialog internals) — and only with a comment naming that reason. Teardown of
   *objects* is never skipped: that is what the stress test covers. The
   2026-10-20 addendum below tightens this for test process exit.
4. **Plugin libraries are process-lifetime.** Registration is dropped on
   unload; the module stays mapped. Loading the same path again reuses the
   already-mapped handle.
5. **New singletons state which policy they follow** in a comment at their
   `instance()` definition.

### Addendum (2026-10-20)

Decision 3 is tightened. Test executables must exit through a normal `return`
from `main()` after orderly shutdown. `TerminateProcess`, `std::_Exit`,
`std::quick_exit`, and deliberately leaking a heap `MainWindow` so its
destructor never runs are not permitted.

Production `main.cpp` and the tests share one entry point:
`mviewer::application::shutdownProcessServices()` (tests typically hold
`mviewer::application::ScopedProcessShutdown` so early `return` still runs it).
Widgets the test owns must already have been destroyed (stack/`unique_ptr`
scope, or `deleteLater` plus draining `DeferredDelete`). The shared sequence,
while `QCoreApplication` is still alive, is:

1. Drain posted events, including `DeferredDelete`.
2. Run registered UI shutdown hooks (`ThumbnailCache` joins its bootstrap and
   invalidation threads).
3. Cancel `MetadataIndexer` requests and `ThumbnailPipeline` work.
4. `FrameSequenceReader::releaseFile({})` so the process-lifetime
   `QImageReader` is destroyed before Qt plugin teardown.
5. Join `TagStore` / `RatingStore` background workers.
6. `TaskScheduler::instance().shutdown()`, then wait for the display-resample
   pool and `QThreadPool::globalInstance()`.
7. Clear the main-thread dispatcher and `shutdownFileLogger()`.
8. Drain posted events again, then return so `QApplication` can be destroyed.

Object-level teardown bugs found while removing the process-kill workarounds
were fixed in production: `ThumbnailPanel` and `DirectoryMonitor` join their
`QThreadPool`s in the destructor body (while `this` is still a valid
`QObject`), and the animation-session `QImageReader` is a leaked
process-lifetime pointer released explicitly at shutdown.

**Remaining exception:** `src/test_workflow_ux.cpp` /
`src/test_workflow_ux_cases.inc` keep one heap `MainWindow` alive after an
offscreen `QFileDialog::getSaveFileName`. That workflow asserts nested-loop
heartbeats of the real dialog; Qt 6.10's offscreen plugin then deadlocks if
the parent `MainWindow` is destroyed. There is no save-dialog injection seam
to avoid opening a real `QFileDialog` here. If upstream Qt fixes the
deadlock, drop this exception.

## Consequences

- No shutdown hook can rely on destructors running for the leaked services:
  anything that must be flushed at exit needs an explicit call (see
  `mviewer::application::shutdownProcessServices()` from `main.cpp` and tests).
- Tests that destroy windows remain safe and must return from `main()` after
  that shared shutdown. A new test must not copy a process-kill or leak-the-
  window pattern for a first-chance object-teardown failure — that is a real
  bug the stress test would catch.
- The offscreen `QFileDialog` → `MainWindow` destruction deadlock is an
  environment property (Qt 6.10 offscreen platform), not a product defect. It
  is the single remaining process-exit exception, named above. If it is fixed
  upstream, the workflow-suite comment and this ADR should be revisited rather
  than silently dropped.

### Addendum (2026-10-25)

`CacheManager`, `ImageCache`, and `DiskCache` join the process-lifetime leak
set alongside `TaskScheduler`. `AsyncTileRequestManager::shutdown` cancels
in-flight tile work but does not join the pooled workers; those workers still
call `CacheManager::storeMipChain` / `getBestMip` (and therefore
`ImageCache` / `DiskCache`) after `main()` returns. Destroying the Meyers
singletons at exit raced a worker and tripped Linux GCC ASan
heap-use-after-free in `m48_phase0_failure_regressions`. Each `instance()` now
allocates with `new` and never destroys; the OS reclaims at process exit.
Object-owned teardown (ImageViewer / CompareWorkspace cancel + generation
guards) is unchanged.

## Status

Accepted (2026-09). Addenda 2026-10-20 and 2026-10-25. Verified by
`teardown_stress_tests` in `.\build.ps1 Test`.
