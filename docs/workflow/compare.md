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
| 4 | Create, move, or resize a linked ROI in any Compare mode | `CompareWorkspace` / Analysis |
| 5 | Toggle blink / diff view | `BlinkController` / `DifferenceEngine` |

## 2. Expected result

- A Compare session is built from the selection with the correct grid layout
  (2 → 2×1, 4 → 2×2, 8 → 4×2).
- Synchronized zoom/pan/selection is honored across cells.
- The difference map for a 2+ image compare is produced with source dimensions.
- Grid, Split, Overlay, Swipe, and Checkerboard show the same source-coordinate
  ROI. Source RGB results remain available in the panel or compact viewport HUD.

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| C-1 | 2 images → 2×1 layout | exact |
| C-2 | 4 images → 2×2 layout | exact |
| C-3 | 8 images → 4×2 layout | exact |
| C-4 | `setSyncEnabled(true)` callable, layout/diff intact | — |
| C-5 | Diff map for 2+ images | non-null, source dims |
| C-6 | Real right-drag creates and mirrors ROI in all Compare modes | exact |
| C-7 | Existing ROI move/edge/corner resize preserves canonical source geometry | exact |
| C-8 | Hidden Analysis panel retains clickable ROI status/results | visible |

## 4. Automated test

**Executables:** `compare_workflow_tests`, `workflow_ux_tests`, and
`m61_roi_workflow_tests`
**What it drives (REAL path, not faked):**
- `CompareEngine::setImages(paths)` + `layout()` for n ∈ {2,4,8} — verifies
  column/row counts (C-1..C-3).
- `engine.setSyncEnabled(true)` then `engine.differenceMap(1,0)` — verifies diff
  (C-4, C-5). This is the same engine `MainWindow::openCompare()` drives.
- Production `CompareWorkspace` receives real right-button press/move/release
  events in every presentation mode, plus move/resize, HUD, clipboard,
  cancellation, backpressure, and source-decoder truth checks (C-6..C-8).

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/compare_workflow_tests
```

## 5. Manual test

1. Load 4 images, open Compare.
2. Zoom one cell — all cells zoom in sync.
3. Right-drag an ROI, then move and resize it — the same rectangle appears on
   all panes and remains editable after switching every presentation mode.
4. Hide Analysis — the compact ROI HUD remains visible and opens the full table.
5. Enable Diff — the difference overlay renders with source resolution.

## 6. Subtraction check (RFC §1)

Documents + gates existing test (`test_compare_workflow.cpp`, M9-2). No new core
code; `CompareEngine`/`SyncController`/`DifferenceEngine` already exist.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
