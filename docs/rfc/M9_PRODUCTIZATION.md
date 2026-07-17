# RFC: M9 — Productization (Professional Image Analysis Workflow)

**Status:** RFC (revised per Tech Lead review 2026-07-17) — for re-approval.
**Date:** 2026-07-17
**Depends on:** M3–M8 (frozen, verified on disk), ADR-001..011.
**Does NOT depend on:** any new infrastructure milestone (M10 deferred).

---

## 0. Context: what already exists (verified 2026-07-17, by file search)

M3–M8 are complete. The engineering substrate is real and frozen:

- **Image Pipeline** (`src/core/image/`): `FileSystem` → `ImageRepository` →
  `DecoderRegistry` → `ImageFrame` → `Cache` → `Viewer`. **Frozen** — do NOT
  redesign or further split (no Manager/Factory/Provider layers).
- **Thumbnail pipeline** (`src/core/thumbnail/ThumbnailPipeline.h`,
  `src/thumbnailcache.cpp`, `src/thumbnailpanel.cpp/.h`) — async generation
  exists; `ThumbnailPanel` UI exists. `test_thumbnailpipeline.cpp` covers it.
- **Compare Engine** (`src/core/compare/`): `CompareEngine` (facade, Qt-free) +
  `SyncController`, `SelectionController`, `ViewportController`,
  `BlinkController`, `DifferenceEngine`, `PixelController` (pixel inspector).
  `CompareWorkspace` (UI) exists. Async diff via JobSystem + EventBus (M4, closed).
- **Render foundation** (`src/core/render/`): `RenderEngine`, `TileCache`,
  `TileGrid`, `Viewport`. **Rendering Foundation, not full architecture** — M9
  does only the minimal Qt-leak cleanup (see §4 Risk #1); no redesign.
- **Analyzers** (`src/core/analyzer/`): `Analyzer` base + **8 concrete**:
  Histogram, RGBMean, PSNR, SSIM, Sharpness, Entropy, Noise. (No Metadata or
  Diff analyzer class — Metadata comes from `ImageFrame::metadata()`, Diff from
  `DifferenceEngine`.) **No AnalyzerRegistry exists** (verified).
- **Command system** (`src/core/command/`): `ICommand`, `CommandStack`,
  `CropCommand`, `RotateCommand`, `LabelCommand`.
- **Domain** (`src/domain/`): `Workspace.h`, `CompareSession.h`, `Selection.h`,
  `Histogram.h`, `Image.h` — **verified Qt-free**.
- **UI surfaces present:** `thumbnailpanel`, `imageviewer`, `compareworkspace`.
- **UI surfaces ABSENT:** `DirectoryTree` (use Qt's `QFileSystemModel` +
  `QTreeView`, do NOT hand-roll), `AnalysisPanel`, Export system.

---

## 1. Goal

Turn the engineering platform into a tool a user can actually use:

> Open directory → browse 1000 images (no UI freeze) → view image →
> compare 2/4/8 → see diff + pixel inspector → run analysis →
> export report/diff → (later) persist workspace.

This is **composition of existing engines + the missing UI ergonomics**
(keyboard shortcuts, drag-drop, context menu, status bar, dock, recent files),
**not** new infrastructure. No new Decoder, Cache, Scheduler, Plugin ABI, GPU
backend, or abstraction layers (Registry/Factory/Manager).

**Review principle (from Tech Lead):** every new module must answer
*"does this make the user faster / more comfortable?"* If no, lower priority —
even if the abstraction is elegant.

## 2. Architecture (frozen boundaries)

```
UI (Qt Widgets)                ← new: AnalysisPanel, Export dialog, shortcuts,
   │ uses                          docks, context menus, status bar, recent files
   ▼
Application (UseCase)          ← Browse / Compare / Analysis / Export use cases
   │ uses
   ▼
Core (frozen engines)          ← ImageRepository, CompareEngine, RenderEngine,
   │                              Analyzer(s), Command, Plugin (all EXIST)
   ▼
Domain (frozen, Qt-free)       ← Workspace, CompareSession, Selection, Histogram
```

Hard rule (ADR-002/009/011): **UI never decodes, never holds pixels, never
directly mutates engine/domain state.** Every user action goes
`UI → Command/UseCase → Core`.

---

## 3. Scope — revised M9 phases (reordered per review)

### M9-1 Browse workflow
- Directory navigation via **`QTreeView` + `QFileSystemModel`** (Qt built-in —
  do NOT implement a custom tree/scanner).
- Wire `DirectoryTree → ThumbnailPanel → ImageViewer` (both exist).
- **Acceptance:** 1000-image directory opens < 1 s; thumbnails generate in
  background; UI never blocks; first thumbnail ~200 ms.
- **Test:** `test_product_browse.cpp` (new) — `loadDirectoryAsync` on a 1000-img
  temp dir, asserts return < 100 ms and frame count == 1000.

### M9-2 Compare workflow
- UI: select image A, Ctrl-select B, right-click → Compare → `CompareWorkspace`.
- 2 / 4 / 8 layouts (engine already supports N via `CompareLayout::forCount`).
  Sync zoom/pan/scroll/selection + Blink + async Diff already in engine.
- **Acceptance:** CompareSession from selection; 2/4/8 cells render; sync active.
- **Test:** `test_compare_workflow.cpp` (new) — session of 2/4/8 paths, asserts
  `imageCount()` + `layout()` cols/rows.

### M9-3 Analysis workflow (YAGNI: NO AnalyzerRegistry)
- `AnalysisPanel` (new UI) holds a **simple `std::vector<Analyzer*>`** of the
  existing analyzers and runs them directly. **No Registry / Factory /
  Plugin descriptor.** Introduce a Registry only if analyzer count exceeds ~10.
- Show: Histogram (HistogramAnalyzer, exists), Metadata (`ImageFrame::metadata()`,
  exists), Pixel Inspector (`PixelController::inspectPixel`, exists in engine),
  plus PSNR/SSIM/Mean RGB for a compared pair (existing analyzers).
- Panel subscribes to pixel/selection changes; reads results; **never calls an
  Analyzer from outside the panel's owned vector**.
- **Acceptance:** panel shows Histogram/Metadata/PSNR/SSIM/Mean RGB for selected
  pair/ROI; no new abstraction layer.
- **Test:** `test_analysis_panel.cpp` (new) — builds panel vector of 8 analyzers,
  runs on a frame, asserts non-empty results.

### M9-4 Export (raised priority — most valuable to algorithm engineers)
- `ExportReport` (core/domain): JSON + CSV of
  `{imageA, imageB, PSNR, SSIM, meanRGB, noise, diffSummary}`.
- `compare_diff.png` via `DifferenceEngine::heatMap` → `ImageBuffer` → PNG.
- UI: Export dialog (Save Diff / Save Report).
- **Acceptance:** export of a 2-image compare produces valid `.json`, `.csv`,
  `.png` with correct metrics.
- **Test:** `test_export.cpp` (new) — export on two known images, assert files
  exist + JSON parses + PSNR/SSIM present.

### M9-5 Workspace persistence (LOWEST priority — deferred in phase order)
- Use existing `domain/Workspace.h` (`Folder`, `ImageSet`). Serialize to
  `.mviewer_workspace` (images / compare / roi / analyzer). Plus "Recent files"
  list (cheap, high value).
- **Acceptance:** save → close → reload round-trips; recent-files list populated.
- **Test:** `test_workspace_persist.cpp` (new) — serialize/parse round-trip.

### M9-6 Polish (product ergonomics — the real gap)
- Keyboard: `Ctrl+O` (open), `Ctrl+S` (save/export), `Delete`, `F11`
  (fullscreen), `Space` (next), `Backspace` (prev).
- Mouse: wheel zoom, drag-drop file/folder open.
- UI: status bar (pixel/hover readout), `QDockWidget` layout, context menu
  (right-click: Compare / Export / Delete / Open), recent-files menu.
- **Acceptance:** all shortcuts/menu actions route via Command; documented in a
  shortcuts reference; manual smoke pass.
- **Test:** `test_shortcuts.cpp` (new) — assert key bindings map to Commands.

---

## 4. Forbidden (explicit non-goals)

1. Do NOT redesign / further split `ImageRepository` (no Manager/Factory/Provider).
2. Do NOT redesign `CacheManager` / `ThumbnailCache`.
3. Do NOT add a new Scheduler (use existing `TaskScheduler`/`JobSystem`).
4. Do NOT do Plugin ABI v1 changes.
5. Do NOT add GPU Backend.
6. Do NOT introduce Flutter / QML.
7. Do NOT rewrite the UI framework.
8. Do NOT introduce `AnalyzerRegistry` / `AnalyzerFactory` in M9 (YAGNI — vector
   of analyzers is enough; revisit at >10 analyzers).
9. Do NOT hand-roll a directory tree — use `QFileSystemModel` + `QTreeView`.
10. Do NOT split `CompareEngine.h` for tidiness (deferred past M9).

### Risk cleanups (minimal, scoped)
- **Risk #1 (RenderEngine Qt leak):** `src/core/render/RenderEngine.h:5-6`
  `#include <QPainter>` / `<QRect>` leak Qt into a Core header. Allowed ONLY a
  minimal fix: define Qt-free `RenderRect`/`RenderSize` (RenderSize already
  exists) and drop the Qt includes — **no RenderEngine behavior change, no scope
  expansion.** If it risks creep, defer to M10.
- **Risk #2 (CompareEngine.h size):** deferred (see forbidden #10).
- **Risk #3 (domain purity):** verified clean — keep it that way.

---

## 5. Acceptance criteria (M9 done = all green)

- [ ] Open 1000-image directory < 1 s, no UI freeze (M9-1).
- [ ] First thumbnail ~200 ms (M9-1).
- [ ] Select 2/4/8 → CompareWorkspace renders with sync (M9-2).
- [ ] AnalysisPanel shows Histogram/Metadata/PSNR/SSIM/Mean RGB via a plain
  analyzer vector, no Registry (M9-3).
- [ ] Export produces valid `.json` + `.csv` + `compare_diff.png` (M9-4).
- [ ] Workspace save/load + recent-files round-trip (M9-5).
- [ ] Shortcuts / context menu / status bar / dock wired via Command (M9-6).
- [ ] `build.ps1 Release` green; `core_tests` + new M9 tests green.

## 6. Test plan (new targets → `src/CMakeLists.txt`)

- `test_product_browse.cpp` → `product_browse_tests`
- `test_compare_workflow.cpp` → `compare_workflow_tests`
- `test_analysis_panel.cpp` → `analysis_panel_tests`
- `test_export.cpp` → `export_tests`
- `test_workspace_persist.cpp` → `workspace_persist_tests`
- `test_shortcuts.cpp` → `shortcuts_tests`

## 7. CI upgrade (minimal, per review)

Current CI is sufficient. Add only:

```
Build → CTest → clang-format check → clang-tidy (warning-only) → Artifact → stop
```

Do NOT add: ASan, UBSan, Coverage, CodeQL (defer).

## 8. Out of scope / deferred to M10 (Performance Engineering)

- Benchmark suite (`benchmark/`: 1000 JPEG/PNG/TIFF; startup, thumbnail gen,
  memory, cache-hit, scroll latency).
- `MemoryTracker` (ImageFrame alloc, cache mem, thumbnail mem, peak).

---

## 9. Review checklist (for Tech Lead re-approval)

- [ ] AnalyzerRegistry removed; AnalysisPanel uses a plain vector (YAGNI)?
- [ ] Phase order: Browse → Compare → Analysis → Export → Workspace → Polish?
- [ ] Export prioritized above Workspace?
- [ ] DirectoryTree = `QFileSystemModel` + `QTreeView` (no custom tree)?
- [ ] ImageRepository NOT split further?
- [ ] RenderEngine cleanup minimal (no redesign)?
- [ ] Every phase ends with acceptance doc + tests + local `build.ps1 Test` green + CI green?
- [ ] Product-experience lens applied ("does this make the user faster?")?
