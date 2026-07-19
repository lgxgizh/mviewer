# Workflow: Workspace (Save → Restart → Restore)

**Phase:** M13 / Product Beta — Phase 1 (Product Workflow verification)
**Owner:** Hermes (commander)
**Status:** VERIFIED (automated) — see `workspace_persist_tests`

---

## 1. User actions

| Step | Action | UI surface |
|------|--------|-----------|
| 1 | Browse / Compare / Analyze a set of images | all panels |
| 2 | Click "Save Workspace" | `MainWindow::saveWorkspace` |
| 3 | Quit MViewer | — |
| 4 | Relaunch MViewer | — |
| 5 | Click "Open Workspace" | `MainWindow::openWorkspace` |

## 2. Expected result

- The workspace JSON captures: open directory, compare session (images +
  ROI), and per-image analysis text.
- After restart, restoring the workspace returns the app to the same state:
  directory re-opened, compare session rebuilt (even with **no ROI / no
  analysis**), per-image analysis reattached.

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| W-1 | Save captures directory + comparedImages + ROI + analysis | round-trips |
| W-2 | Restore reopens the directory | exact path |
| W-3 | Restore rebuilds the compare session (incl. no-ROI/no-analysis edge) | exact set |
| W-4 | Restore reattaches per-image analysis text | exact |
| W-5 | Legacy workspace files (no `comparedImages` key) still load | tolerant |

## 4. Automated test

**Executable:** `workspace_persist_tests` (ctest: `workspace_persist_tests`)
**Source:** `src/core/test_workspace_persist.cpp`
**What it drives (REAL path, not faked):**
- Serializes a `Workspace` (domain object) via `WorkspaceSerializer`, then
  deserializes — asserts directory / `comparedImages` / ROI[4] / analysis
  survive (W-1, W-2, W-3, W-4).
- **Edge case (review P0):** a compare session with **no ROI and no analysis**
  round-trips via the explicit `comparedImages` array (W-3).
- **Legacy tolerance (W-5):** a JSON missing `comparedImages` defaults to empty
  and loads without throwing.

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/workspace_persist_tests
```

## 5. Manual test

1. Open a folder, compare 2 images (no ROI drawn), analyze one.
2. Save Workspace to `session.mvws`.
3. Quit, relaunch, Open Workspace.
4. Confirm: same folder, same 2-image compare, analysis text present.

## 6. Subtraction check (RFC §1)

Documents + gates existing test (M9-5). Closes review **P0** edge case via the
explicit `comparedImages` array. No new core code; `WorkspaceSerializer` already
exists.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/domain/Workspace.h`,
`docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
