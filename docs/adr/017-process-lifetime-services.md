# ADR 017 — Process-lifetime services and Qt teardown

## Context

The repository contains a family of "do not tear this down" workarounds that were
accumulated one bug at a time and had no single written rationale. A 2026-09
review flagged them as a HIGH-severity risk ("MainWindow teardown is not proven")
because a reader cannot tell an intentional platform workaround from forgotten
debt:

- `src/test_mainwindow_commandstack.cpp` ends both verdicts with
  `terminateTestProcess()` (`TerminateProcess` + `std::abort`) so the stack
  `MainWindow` and `QApplication` are never destroyed.
- `src/test_workflow_ux.cpp` leaves its last window to process teardown: an
  offscreen `QFileDialog` leaves internal modal widgets that deadlock
  `MainWindow` destruction.
- `src/test_workflow_ux_cases.inc` keeps one `MainWindow` on the heap for the
  same reason; `src/test_m57_multiframe.cpp` exits with `std::_Exit`.
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
   dialog internals) — and only with a comment naming that reason, as the
   existing sites now do. Teardown of *objects* is never skipped: that is what
   the stress test covers.
4. **Plugin libraries are process-lifetime.** Registration is dropped on
   unload; the module stays mapped. Loading the same path again reuses the
   already-mapped handle.
5. **New singletons state which policy they follow** in a comment at their
   `instance()` definition.

## Consequences

- No shutdown hook can rely on destructors running for the leaked services:
  anything that must be flushed at exit needs an explicit call (see
  `mviewer::core::shutdownFileLogger()` called from `main.cpp`).
- Tests that destroy windows remain safe; tests that merely *end* still pass,
  but a new test must not copy the terminate-the-process pattern for a
  first-chance object-teardown failure — that is a real bug the stress test
  would catch.
- The offscreen `QFileDialog` → `MainWindow` destruction deadlock is an
  environment property (Qt 6.10 offscreen platform), not a product defect. If
  it is fixed upstream, the workflow-suite comment and this ADR should be
  revisited rather than silently dropped.

## Status

Accepted (2026-09). Verified by `teardown_stress_tests` in `.\build.ps1 Test`.
