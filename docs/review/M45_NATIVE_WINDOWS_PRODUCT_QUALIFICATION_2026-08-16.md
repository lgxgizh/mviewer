# M45 — Native Windows Product Qualification & Desktop Workflow Convergence

Date: 2026-08-16
Scope: CommandStack safety, asynchronous file operations, clipboard paste
lifecycle, delete semantics, and the native-Windows qualification boundary.

## Verdict

**AUTOMATED PASS for the implemented scope; native target-hardware sign-off is
MANUAL / BLOCKED.** The repository has a deterministic CommandStack callback
regression and core transfer-safety coverage. The isolated production UI
acceptance reaches MainWindow → ThumbnailPanel → Rename → Undo/Redo and
Ctrl+V PNG paste. It does not claim that an offscreen or developer desktop run
proves ICC, mixed-DPI, multi-volume, native-dialog, or long-session behavior.

## Implemented closure

- `CommandStack` now serializes mutations separately from its history mutex.
  `ICommand` work runs without the state mutex, and change callbacks are copied
  and invoked after unlocking it. `recordExecuted()` provides a short UI-thread
  commit for worker-completed commands.
- `test_commandstack` covers callback queries after execute/undo/redo/clear,
  ordinary failure, unresolved failure/recovery, and cancellation safety: **54
  passed, 0 failed**. `commandstack_callback_regression` passes with a 3-second
  watchdog and exercises the former callback re-entry cycle.
- File move/copy/delete paths use `TaskScheduler`, progress observers, cancel
  checks, atomic copy verification, rollback, and generation/alive guards. The
  UI progress callback only queues value updates to the GUI thread.
- Ctrl+V captures the clipboard `QImage` on the GUI thread, encodes a UUID-named
  PNG on the I/O scheduler, then re-enters `onImageOpen()` only for the current
  generation. Stale files are cleaned on startup/teardown and old paste files
  are aged out.
- Delete is explicitly labeled **MViewer 回收站**. It is a per-user reversible
  staging directory, not the native Windows Shell Recycle Bin.

## Evidence and limitations

| Area | Evidence | Status |
|---|---|---|
| Full Release gate | `build.ps1 Test` on 2026-08-16 | 90/90 PASS |
| Architecture boundary | `architecture_gate.ps1 -Json` | 0 warnings PASS |
| Complexity boundary | `complexity_gate.ps1 -Json -Strict` | 0 hard failures PASS (96 advisory warnings) |
| CommandStack callback deadlock | `test_commandstack --callback-probe` + CTest watchdog | PASS |
| Command/file transfer safety | `test_commandstack`, 54/54 | PASS |
| MainWindow callback surface | `mainwindow_commandstack_acceptance` | PASS in isolated UI harness |
| Clipboard PNG lifecycle | MainWindow acceptance + generation/cleanup code | PASS in isolated UI harness |
| Native Windows Release GUI | Requires an interactive target desktop | MANUAL / BLOCKED |
| ICC / DPI / mixed-DPI | Requires physical display profiles and 100/125/150/200% runs | MANUAL / BLOCKED |
| Real two-volume move/copy | Requires two writable volumes | MANUAL / BLOCKED |
| Native Unicode dialogs | Requires interactive native file dialogs | MANUAL / BLOCKED |
| Long-session soak / RSS / handles | Requires target-duration interactive observation | MANUAL / BLOCKED |

## Required next evidence

Run the Release executable on the target Windows desktop and attach dated
screenshots/logs for Browse → Viewer → Compare → Analyze → Export → FileOps,
ICC profile changes, mixed-DPI transitions, two physical volumes, native
dialogs, and a long-session resource observation. Do not convert these rows to
PASS from offscreen CTest results alone.
