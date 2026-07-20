# RFC M14 — Product Shell (make MViewer openable & daily-usable)

**Status:** PROPOSED
**Date:** 2026-07-20
**Owner:** Hermes (commander)
**Phase:** Product Beta — Phase 1 (per review 2026-07-19 / 2026-07-20)

---

## 0. Context (ground truth, verified 2026-07-20)

A full source-tree audit (after an initial misread was corrected) shows the
**engine AND the cockpit largely exist**:

- `src/main.cpp` + `src/mainwindow.cpp` + `imageviewer.cpp` + `directorytree.cpp`
  + `thumbnailpanel.cpp` + `previewpanel.cpp` + `compareworkspace.cpp` +
  `analysispanel.cpp` + `exportdialog.cpp` all present; `MViewer.exe` builds
  from a clean configure (verified: `build.ps1 Clean && Test` => 32/32, exe built).
- `MainWindow` already wires DirectoryTree → ThumbnailPanel → ImageViewer →
  PreviewPanel → AnalysisPanel → CompareWorkspace, with menu/statusbar,
  ←/→ navigate, Space preview, F fullscreen, Workspace save/restore.
- The original "no cockpit" assessment was **wrong** (a tooling/assumption
  error, not reality). This RFC therefore covers **productization gaps**, not
  building a shell from scratch.

**Real P0 gaps (your ⭐⭐⭐⭐⭐, Phase-1 ①):** the Browse experience lacked
*product memory* — no Recent Folders, no Favorites, no in-session History
(back/forward), and no cross-session restore of browse position (last folder +
image + thumbnail scroll). `core::RecentFiles` existed but was unused by the UI.

**P0 delivered (this RFC):** Recent Folders menu (from `RecentFiles` LRU),
Favorites menu + "收藏当前目录" (Ctrl+D), in-session History back/forward
(Alt+Left/Alt+Right), and cross-session restore of last folder + last image +
thumbnail scroll position via a new `AppState` JSON (per-user config). Verified
by `test_appstate` (persistence round-trip, all pass) and clean offscreen launch.

---

## 1. Goal

Turn MViewer from "a library with tests" into "an application a person opens and uses daily." Scope of M14 = the **shell + Browse Workflow** (highest-priority item, ⭐⭐⭐⭐⭐). Later milestones extend it (Compare/Workspace productization, perf-budget CI gate, analyzers, release) — those are separate RFCs building on this shell.

M14 delivers, end-to-end and verified:
1. A `mviewer` Qt6 executable (`QApplication` + `QMainWindow`) that launches.
2. **Browse Workflow**: Open Folder → thumbnail grid streams in (non-blocking) → click thumbnail → `RawImageView` shows full image → Next/Previous (keyboard + buttons) → scroll thumbnail grid stays responsive → close & reopen restores last folder + scroll position.
3. History / Recent Folders (reusing existing `RecentFiles`).
4. Build + the existing `.\build.ps1 Test` gate stays green; new UI shell has its own smoke test (headless, offscreen).

---

## 2. Reuse (do NOT rebuild — architecture is frozen)

| Existing asset | Use in shell |
|---|---|
| `OpenDirectoryUseCase::execute(path, max)` | list images in a folder |
| `ThumbnailPipeline` (singleton) | background thumbnail decode + visible-range priority + LRU; `setSources`/`setVisibleRange`/`request`/`setResultFn` |
| `RawImageView` (widgets) | main image view (zoom/pan/ROI/overlay already implemented) |
| `ImageRepository::loadDirectoryAsync` | full-image async load for the viewer |
| `WorkspaceSerializer` + `RecentFiles` | recent folders + last-session restore |
| `CompareImagesUseCase` / `DeleteImageUseCase` / `RenameImageUseCase` | wired in later milestones (M15/M16), not M14 |

---

## 3. New components (minimal, UI-layer only)

```
src/ui/
  MainWindow.cpp/.h         QMainWindow: menu, dock layout, glue
  BrowsePanel.cpp/.h       DirectoryTree + ThumbnailGrid + status bar
  ThumbnailGrid.cpp/.h      QListView/QAbstractItemModel over ThumbnailPipeline
  App.cpp/.h               QApplication entry (int main)
CMakeLists.txt             add mviewer executable target (Qt6 Widgets)
```

- `ThumbnailGrid` adapts `ThumbnailPipeline` results (`ImageData` → `QPixmap`) via the injected `ResultFn`, posting to the UI thread with `QMetaObject::invokeMethod` / `Qt::QueuedConnection`.
- `BrowsePanel` owns the `ThumbnailPipeline` instance, calls `setVisibleRange` on scroll, and tracks the current index for Next/Previous.
- `MainWindow` persists `lastFolder` + `lastIndex` to a small JSON (reusing `WorkspaceSerializer` helpers or a tiny `AppState`) and restores on launch.
- **No Qt types in `core/` or `domain/`** (AGENTS.md rule). All Qt lives in `src/ui` + `src/widgets`.

---

## 4. Acceptance criteria (M14)

| ID | Criterion | Target | Test |
|----|-----------|--------|------|
| S-1 | App launches to an empty shell, no crash | — | `mviewer --selftest` / headless smoke |
| S-2 | Open 1000-image folder returns immediately (UI not blocked) | call < 100 ms | `product_browse_tests` (exists) + UI smoke |
| S-3 | First visible thumbnail appears | < 200 ms (CI ceiling) | `product_browse_tests` |
| S-4 | Next/Previous switches image | < 100 ms (cold) / < 16 ms (preloaded) | UI smoke + `RawImageView` render |
| S-5 | Thumbnail grid scroll stays responsive | no UI freeze | manual + `product_browse_tests` |
| S-6 | Close + reopen restores last folder + index | 100% | UI state test (headless) |
| S-7 | Recent Folders list populated | ≥ 1 entry | unit (`RecentFiles` exists) |

---

## 5. Subtraction check (RFC §1)

- No change to `build.ps1`, `CMakePresets.json`, `ci.yml`, `core/`, `domain/`.
- Only **adds** a `src/ui` layer + one executable target. The frozen build entry (`build.ps1 Test`) still governs; the new executable is built by the same CMake.
- Does not refactor `ImageRepository`, `Cache`, or `RenderEngine` (per review: stop infra work).

---

## 6. Verification

- `.\build.ps1 Test` must stay green (31/31) — existing gate untouched.
- New: `mviewer` builds and a headless offscreen smoke (`QT_QPA_PLATFORM=offscreen`) opens a folder, switches 3 images, exits 0.
- Manual: open `D:\test_images` (or a generated 1000-image corpus), confirm non-blocking stream + smooth scroll + restore.

---

## 7. Out of scope (later RFCs, same shell)

- M15 Compare Workspace productization (save/restore session 100%)
- M16 Workspace persistence (layout/ROI/analyzer/recent)
- M17 Performance-budget **CI gate** (promote nightly budget → PR-blocking)
- M18 Analyzer extensions (MTF, Dead Pixel, Color Checker, …)
- M19 Tile + GPU Upload pipeline (100MP)
- M20 Release (installer/portable/symbols/crash dump/auto-update)

These reuse the M14 shell; M14 is the foundation they all stand on.
