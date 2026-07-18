# Product Workflow Acceptance — M11 Product Beta

**Status:** DRAFT for review (Phase 1 of the M11 Product-Beta plan).
**Author:** Hermes (commander) — grounded in the current `master` tree at
`547982a` (v1.0.0-rc), not in the stale mviewer-master.zip the review was based on.
**Purpose:** Define the real end-to-end user workflow, the gaps that actually
exist today, and verifiable acceptance criteria — before any implementation.

> Discipline: this document is **RFC-first**. No code changes are made until the
> review approves. The "missing features" below are claims verified against the
> source tree, not assumptions.

---

## 0. Reality check on the review's premises

The review (based on an older zip) made several premises that are **already
resolved** in the current tree. Surfacing them so we don't re-do finished work:

| Review premise | Current truth (verified) |
| --- | --- |
| "first thumbnail 2400ms — must be solved" | That was a **benchmark defect** (B2 drove `loadDirectoryAsync`, not the real pipeline). Fixed: B2 now measures the real `ThumbnailPipeline`. Verified `--enforce`: cold 11–20 ms, warm ≤20 ms. Not a product defect. |
| "Does `AnalyzerRegistry` really exist (`registerAnalyzer`/`getAnalyzer`/`runAnalyzer`)?" | **Exists**, factory-based (plugin-safe): `registerAnalyzer(id, AnalyzerCreator)`, `create(id)`, `availableAnalyzers()`, `infoFor(id)`, `queryByCapability(cap)`. Wired into `AnalysisPanel` (registry-driven dropdown; ROI via `create(id)->analyzeRegion(frame, selection)`). The review's proposed `registerAnalyzer(std::make_unique<Analyzer>())` API is inferior (no cross-module-safe deleter) — do **not** replace the existing one. |
| "Core headers may leak Qt (`#include <QWidget>/<QPainter>/<QImage>`)" | `core/**/*.h` already Qt-free by construction (AGENTS.md rule + CI). `Analyzer.h`, `ImageFrame.h`, `CompareEngine.h`, `FileSystem.h`, `ThumbnailPipeline.h` all use `std`/`domain` only. The review's P5 scan is a no-op today. |
| "Singletons may leak / lack shutdown" | `EventBus::instance().unsubscribe()` is called in `~CompareWorkspace` (lifecycle bug already handled). `MemoryTracker` is documented process-global. No unbounded singleton growth found. |

**Genuinely open items (the real work):** Workspace **persistence** (save/load
to disk) is not wired; `FileSystem::listImages` caps at 2000 (truncates >2000-img
dirs); `nightly.yml` does not exist; no real-dataset benchmark (`benchmark/data/`);
Tile Render needs a formal RFC (foundation exists in M7: `Viewport`/`TileCache`).

---

## 1. The real workflow chain (as wired today)

```
OpenDirectoryCommand
   └─ OpenDirectoryUseCase::execute(dir)  → FileSystem::listImages (cap 2000)
        └─ DirectoryTree (QFileSystemModel, async)        [left panel]
        └─ ThumbnailPanel::setDirectory(dir)
             └─ ThumbnailPipeline (visible=Thumbnail prio, neighbors=Thumbnail prio,
                                   after visible; B2-scan ~5-8ms, first thumb 11-20ms)
                  └─ ThumbnailPanel cells (LRU, UI update on callback)
   thumbnail clicked  → PreviewPanel + AnalysisPanel::setImage/setFrame
   thumbnail dbl-click→ ImageViewer (zoom/pan/ROI/pixelInfo)  [separate window]
   thumbnail compareRequested → CompareWorkspace (sync zoom/pan/ROI/blink/diff, EventBus async diff)
   AnalysisPanel combo → AnalyzerRegistry::create(id)->analyzeRegion(frame, Selection)
   Ctrl+S → ExportCommand → ExportDialog → core::buildCompareReport + Encoder (JSON/CSV/PNG)
   Workspace: scanned into domain::Workspace model (ImageRepository::loadWorkspace) — NO disk persistence
```

**Verified green this session:** `core_tests` (ALL_COMPARE_OK — sync/blink/diff/
async-EventBus), `export_tests` (13/13 — real report+diff PNG), `mviewer_bench
--enforce` (B2/B8/B9 PASS), `MViewer.exe` headless launch clean.

---

## 2. Acceptance cases (from the review, made measurable)

### Case 1 — Open a 1000-image directory
- UI does not freeze during scan (off the main thread).
- First thumbnails appear; scrolling stays fluid.
- **Metrics (must hold on this machine):**
  - `directory_scan` (ThumbnailPipeline `scan` stage) < 500 ms — *currently ~5–8 ms for 200-img corpus; must re-measure at 1000 with real dataset.*
  - `first_thumbnail` < 300 ms cold / < 50 ms warm — *currently 11–20 ms cold (synthetic). Real 1000-img dataset re-measure required.*
  - UI freeze (main-thread block) < 100 ms.
- **Gap:** `FileSystem::listImages` caps at **2000** — a 1000-img dir is fine, but
  the review's 10000-img target will **truncate**. Either raise the cap or page it.

### Case 2 — Two-image Compare
- thumbnail click → CompareWorkspace; sync zoom, sync pan, ROI, blink, diff all work.
- **Verified by `core_tests`** (layout/sync/blink/diff + non-blocking async diff via
  EventBus). Acceptance **already met**; no code change needed.

### Case 3 — Analysis
- `ImageFrame → AnalyzerRegistry → AnalysisPanel` for ≥ Histogram, RGB Mean, PSNR,
  SSIM, Sharpness.
- **Verified:** `AnalysisPanel` dropdown is registry-driven; built-ins include
  histogram/noise/entropy/psnr/sharpness/ssim/rgbmean (M4). Acceptance **already met**.

---

## 3. Missing features / current gaps (the real list)

1. **Workspace persistence (P0 chain break):** the flow ends at "scan into
   `domain::Workspace` model." There is **no `save()`/`load()` to disk** and no
   wiring in `MainWindow`. The review's chain "保存 Workspace" is unimplemented.
   *This is the one true P0 gap.* (Domain model exists; only persistence + UI hook missing.)
2. **`FileSystem::listImages` 2000 cap:** truncates large dirs; blocks the 10000-img
   benchmark target. Needs a higher cap or streaming/paging.
3. **No real benchmark dataset:** `mviewer_bench` generates a synthetic corpus
   (512×512). The review wants `benchmark/data/{small,medium,large}` = 100/1000/10000
   real JPEGs, plus B1 scan p50/p95/p99 percentiles.
4. **No `nightly.yml`:** CI is Phase-1 only (format/build/test/package). Review wants
   a non-blocking nightly running clang-tidy + ASan + benchmark.
5. **Tile Render RFC missing:** M7 laid `Viewport`+`TileCache`+`TileGrid` foundation;
   a formal RFC (CPU tile, 12000×8000 JPEG, visible-region-only load) is not written.

---

## 4. Acceptance criteria (this document's bar)

- [ ] **Workflow doc approved by review** (this file) — gate before any code.
- [ ] Case 1/2/3 reproduce on a **real 1000-img dataset** with the metrics in §2 met.
- [ ] Workspace **save + load** round-trips (open dir → analyze → save → reopen →
      state restored). New acceptance test `workspace_persist_tests` extended to disk.
- [ ] `FileSystem::listImages` handles ≥10000 imgs without truncation (cap raised or paged).
- [ ] `benchmark/data/` present; B1 reports p50/p95/p99 scan latency.
- [ ] `nightly.yml` added (non-blocking).
- [ ] Tile Render RFC written and reviewed (no implementation yet).

---

## 5. Explicit non-goals (per review + AGENTS.md freeze)

- ❌ Rewrite `ImageRepository` / `CacheManager` / `RenderEngine`.
- ❌ Introduce Rust.
- ❌ New abstraction layers (`ImageManager`/`ImageProvider`/`ImageFactory`).
- ❌ Modify `build.ps1` / `CMakePresets.json` / `ci.yml` (except adding `nightly.yml`).
- ❌ clang-tidy/ASan as PR-blocking gates (keep advisory; nightly only).

## 6. Proposed execution order (after review approval)

1. **M11.1 Workflow Complete** — Workspace persistence + UI hook; close the P0 gap.
2. **M11.2 Performance** — real-dataset benchmark; confirm first-thumbnail <300ms cold
   on 1000 real imgs; raise `listImages` cap.
3. **AnalyzerRegistry convergence** — already real; add an acceptance test asserting
   every built-in is creatable + produces non-empty `resultText()` (covers Case 3).
4. **Tile Render RFC** — write the RFC; implement CPU-tile loader only after RFC approval.
5. **M11.3 Release Engineering** — installer/portable zip, README, screenshots, demo gif.
