# Workflow: Compare (Viewer → Compare session)

**Phase:** M13 / Product Beta — Phase 1 (Product Workflow verification)
**Owner:** Hermes (commander)
**Status:** VERIFIED (automated) — see `compare_workflow_tests`

---

## 1. User actions

| Step | Action | UI surface |
|------|--------|-----------|
| 1 | Open an image in the Viewer | `ImageViewer` / `RawImageView` |
| 2 | Select one or more images (Ctrl/Shift) in ThumbnailPanel | `ThumbnailPanel` |
| 3 | Click "Compare" / `CompareCommand` | `CompareWorkspace` |
| 4 | Toggle synchronized zoom / pan / ROI | `CompareWorkspace` toolbar |
| 5 | Toggle blink / diff view | `BlinkController` / `DifferenceEngine` |

## 2. Expected result

- A Compare session is built from the selection with the correct grid layout
  (2 → 2×1, 4 → 2×2, 8 → 4×2).
- Synchronized zoom/pan/selection is honored across cells.
- The difference map for a 2+ image compare is produced with source dimensions.

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| C-1 | 2 images → 2×1 layout | exact |
| C-2 | 4 images → 2×2 layout | exact |
| C-3 | 8 images → 4×2 layout | exact |
| C-4 | `setSyncEnabled(true)` callable, layout/diff intact | — |
| C-5 | Diff map for 2+ images | non-null, source dims |

## 4. Automated test

**Executable:** `compare_workflow_tests` (ctest: `compare_workflow_tests`)
**Source:** `src/core/test_compare_workflow.cpp`
**What it drives (REAL path, not faked):**
- `CompareEngine::setImages(paths)` + `layout()` for n ∈ {2,4,8} — verifies
  column/row counts (C-1..C-3).
- `engine.setSyncEnabled(true)` then `engine.differenceMap(1,0)` — verifies diff
  (C-4, C-5). This is the same engine `MainWindow::openCompare()` drives.

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/compare_workflow_tests
```

## 5. Manual test

1. Load 4 images, open Compare.
2. Zoom one cell — all cells zoom in sync.
3. Draw an ROI on one cell — appears on all.
4. Enable Diff — the difference overlay renders with source resolution.

## 6. Subtraction check (RFC §1)

Documents + gates existing test (`test_compare_workflow.cpp`, M9-2). No new core
code; `CompareEngine`/`SyncController`/`DifferenceEngine` already exist.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
