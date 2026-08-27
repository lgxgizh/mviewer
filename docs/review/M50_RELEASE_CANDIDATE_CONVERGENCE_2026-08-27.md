# M50 — Release Candidate Workflow Convergence & Native Windows Qualification

Date: 2026-08-27  
Branch: `master`  
Target patch line: `1.0.11`

## Scope

M50 closes the release-candidate workflow seams identified after the M49
Windows/Unicode/Compare review:

- one committed owner for directory transitions and their directory-scoped
  side effects;
- non-blocking, latest-wins Sidecar import with UI convergence;
- Compare rendering responsibility split into bounded translation units;
- platform-aware command-line path classification;
- repeatable evidence for the Browse → Select → Compare → Analyze → Export
  workflow and for teardown/drain behavior.

## Baseline and reproduction

The pre-M50 code had several independent directory entry paths. The
`directoryChanged` connection owned the gallery, breadcrumb, model, recents,
history, status, empty-state, and reindex updates, while `changeDirectory()`
also pushed history and scheduled Sidecar work. Tree clicks and refresh did not
schedule Sidecar at all, and Sidecar completion had no UI delivery step to
re-evaluate the active rating/flag filters. `pushDirHistory()` could also prune
the forward branch during Back navigation. Finally, the startup parser treated
every `/...` argument as a switch, which rejects Unix absolute paths.

The initial M50 characterization run was intentionally executed before the
fixes. It reproduced five failures:

1. the active rating filter did not converge after Sidecar import;
2. Sidecar rating/label/pick/reject state was not restored in the visible flow;
3. the published current image was lost during convergence;
4. Back did not re-run Sidecar convergence;
5. Forward was lost after Back.

The Compare render implementation was also concentrated in one 1,854-line
translation unit, mixing LOD geometry, pane materialization, Diff batches, and
canvas/Blink painting.

## Implemented closure

### Directory and Sidecar convergence

- `MainWindow::changeDirectory()` now only validates and requests the
  `DirectoryTree` transition.
- The `directoryChanged` connection is the single committed-transition owner
  for path edit, tree navigation, breadcrumb, Back/Forward, refresh, and
  restore. It publishes the directory models/history and starts the
  cancellable Sidecar import from the same boundary.
- Sidecar completion is marshalled back to the UI thread, guarded by both
  `MainWindow` lifetime and the latest directory intent. The active rating
  filter is re-applied after import, and the metadata panel is refreshed for
  the current image.
- Existing history entries are recognized before forward-branch pruning, so
  Back followed by Forward remains a valid navigation sequence.

### Compare and command line

- Compare rendering is split into materialization, Diff, and canvas/Blink
  translation units. The resulting files are 576, 590, 339, and 368 lines;
  behavior and public interfaces are unchanged.
- `application/CommandLine.h` keeps `-` options reserved everywhere, reserves
  `/` switches only on Windows, and permits Unix absolute paths on Unix. The
  Windows drive and UNC path cases remain positional file arguments.

## Automated evidence

| Check | Result | Evidence |
|---|---|---|
| M50 navigation/Sidecar/teardown | PASS | `m50_navigation_tests` |
| Command-line path classification | PASS | `commandline_tests` |
| Canonical Workflow UX | PASS | `workflow_ux_tests` |
| Browse convergence UI | PASS | `browse_convergence_ui_tests` |
| M48 phase-0 regressions | PASS | `m48_phase0_regressions` |
| M46 browse and persistence | PASS | `m46_browse_tests`, `m46_persistence_tests` |
| Full local build/test gate | PASS — clean `build.ps1 Test`, 107/107 | `build.ps1 Clean`, then `build.ps1 Test` |
| Release ZIP assembly | PASS — 1.0.11 payload, 50 entries, no test executables/PDBs | exact release ZIP command |

The M50-focused and adjacent-workflow checks passed 7/7 after the fix. The
clean full gate now passes 107/107, and the versioned package contains the
expected application/Qt payload with test executables and PDBs excluded.

## Native Windows qualification boundary

The repository has a Windows MSVC build environment, but this run does not
constitute physical target qualification on a clean end-user machine. The
following remain **MANUAL / BLOCKED**, not automated PASS:

- fresh-machine launch and uninstall/install upgrade;
- real Explorer shell/open-with and file-association handoff;
- DPI changes across monitors, fractional scaling, and color-managed display;
- long-path policy, UNC shares, removable/multi-volume paths, and network
  interruption during Sidecar import/export;
- hardware-specific GPU/driver behavior and long-session human UX review.

These rows require a physical Windows qualification pass and are intentionally
not hidden behind CTest or a relaxed timeout.

## Release verdict

M50 implementation, the clean automated gate, and the versioned release ZIP
are complete. Native target-machine rows remain a separate manual
qualification boundary as listed above.
