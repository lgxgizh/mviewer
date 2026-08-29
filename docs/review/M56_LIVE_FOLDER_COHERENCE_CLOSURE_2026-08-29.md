# M56 — Live-folder Coherence Closure

Date: 2026-08-29 · Milestone: M56

## Result

M56 implements live-folder reconciliation at the active Browse boundary. A
filesystem hint is now a debounced/coalesced mutation signal; committed
directory navigation remains the only path that changes the Browse directory.
The gallery applies row-local changes, preserves path-based selection/current
state and scroll anchoring, and keeps the last coherent rows visible while an
active directory is temporarily unavailable.

## Delivered behavior

| Contract | Implementation | Proof |
|---|---|---|
| Snapshot and delta are UI-independent | `core/filesystem/DirectorySnapshot.{h,cpp}` uses standard-library value types and detects add/remove/modify/unique rename plus separate `.xmp` changes | `m56_directory_snapshot_tests` |
| Active directory owns watching | `DirectoryMonitor` owns one active path, a 75 ms debounce, one worker scan, latest-wins generation and dirty-again scheduling | `m56_directory_monitor_tests` |
| Incomplete writes settle | A changed candidate is re-scanned after a 100 ms stability window, with three bounded retries before commit | monitor stability path |
| Tree expansion cannot steal Browse ownership | `DirectoryTree` keeps a bounded tree-only watcher set; current Browse monitoring is independent | `m50_navigation_tests` plus monitor integration wiring |
| Gallery mutation is incremental | `ThumbnailPanel::applyDirectoryDelta()` uses `insertRows`, `removeRows` and `setData`; it does not call `setStringList()` | `m56_live_gallery_tests` asserts zero model resets |
| Selection/current continuity | Renames migrate paths; removal selects the surviving row at the old position; insertions do not clear the current item | real `SelectionModel` gallery test |
| Overwrite invalidation | Pipeline path revisions, millisecond thumbnail keys, asynchronous historical-cache purge, metadata millisecond keys, and repository previous/current-key invalidation | overwrite case in gallery test and existing cache/repository gates |
| Sidecar locality | Sidecars never enter the image list; only matching image paths are read and the active filter is re-evaluated row-locally | sidecar delta routing in MainWindow |
| Unavailable is not empty | An unavailable delta keeps the last coherent gallery visible and emits an explicit status-bar state; recovery is marked separately and replaces stale rows | snapshot, monitor and gallery tests |

## State boundary

```text
committed navigation A -> B
        │
        └── sets active monitor path + starts a baseline scan

watcher/F5/self-generated operation hint
        │  (debounce/coalesce, bounded async snapshot)
        ▼
DirectorySnapshot diff
        ├── sidecar-only -> matching image metadata import
        ├── unavailable  -> retain coherent rows + explicit status
        └── image delta  -> row-local gallery/cache/viewer update
```

Self-generated file operations still produce watcher hints; the operation
completion no longer performs a duplicate full `setDirectory()` refresh.

## Deterministic evidence

The focused M56 run passed all four registered tests in 1.97 s:

- `m56_directory_snapshot_tests`
- `m56_directory_monitor_tests` — 1,102 explicit hints, a 1,000-notification
  no-op storm, disappearance/recovery, and a physical-scan bound
- `m56_live_gallery_tests` — add/rename/overwrite/remove/recovery with zero
  `modelReset` signals
- `m56_live_directory_soak` — deterministic 10,000-entry classification soak

The monitor test asserts fewer than 20 physical scans for the full scenario;
the soak verifies 10k-entry add/remove/modify/rename classification without
filesystem-order dependence.

The final source-stable qualification pair is:

| Round | Command | Result |
|---|---|---|
| 1 | `powershell -ExecutionPolicy Bypass -File D:\\mviewer\\build.ps1 Test` | 123/123 passed, 746.05 s |
| 2 | `powershell -ExecutionPolicy Bypass -File D:\\mviewer\\build.ps1 Test` | 123/123 passed, 743.98 s |

Both rounds used the Release build path and included `bench_enforce`,
`bench_smoke`, `golden_image`, the workflow gates, architecture and complexity
regressions, and all four M56 tests.

## Boundaries and remaining manual qualification

- Rename inference uses a unique file identity where available and a
  size/mtime/extension fallback. Ambiguous matches intentionally remain
  add+remove rather than guessing.
- The watcher is a hint source, not the source of truth. F5 and self-generated
  operations use the same active monitor reconcile path.
- Native Windows Explorer/UNC/DPI interaction feel, long-session perceived
  smoothness, and physical directory replacement remain `MANUAL/BLOCKED` until
  they are exercised on the target desktop. No manual-only result is reported
  as automated PASS.
