# MViewer Architecture (Target Architecture)

> This document is the **target architecture**, confirmed by user review (2026-07-11).
> **M2 architecture freeze is complete (2026-07-13)** — the target architecture described in sections 3–5 below is fully implemented,
> and the code and specification documents are frozen together. Subsequent work evolves milestone by milestone toward M3 (analysis panel), M4 (export / format conversion),
> M5 (performance profiling), M6 (disk cache deepening), and M7 (CommandPalette integration).
>
> **Detailed architecture specifications** are in `vision.md`, `roadmap.md`, and 8 English specification documents in the same directory.

## 1. Product Positioning (Most Critical)

MViewer is **not an image browser**. It is an **image algorithm verification tool**
(comparable to FastStone Pro plus algorithm analysis). The core workflow is **comparison** and **analysis**;
browsing is only the entry point. Therefore the compare / analysis / render engines must be independent of `QWidget`.
Otherwise all logic piles into `Viewer.cpp` and becomes 5,000 lines of unmaintainable code.

## 2. Technology Choices

| Item | Decision | Notes |
| ---- | ------ | ------ |
| Language | C++20 | ✅ Verified |
| GUI | Qt 6 **Widgets** (not QML) | ✅ Verified |
| Toolchain | **MSVC 2022** (target achieved) | M2 completed the switch from MinGW to MSVC |
| Build | CMake + Ninja | ✅ Verified |

## 3. Target Layered Architecture (✅ Frozen and landed in M2)

```
MainWindow
   │
   ├── DirectoryTree
   ├── ThumbnailPanel
   ├── CompareWorkspace        ← the real bulk of the work, not the directory tree
   │      ├── Render Engine
   │      └── Analysis Engine
   ├── AnalysisPanel
   ├── StatusBar
   └── CommandPalette          ← UI deferred to M7 (interface already frozen)

Compare Engine   synced zoom / synced pan / synced marquee selection / synced scroll / blink compare / difference image
Analysis Engine  Pixel Inspector / Histogram / RGB Mean / Difference → PSNR / SSIM / Noise
Render Engine    GPU scaling / high-quality interpolation / Difference Overlay / HeatMap

domain/           pure business objects (Image / Histogram / Selection / CompareSession; zero Qt dependency)
   ↑
core/            infrastructure layer (Image Core / Scheduler / Cache / EventBus / Analyzer)
   ↑
application/     use-case layer (UseCases)
   ↑
ui/              QWidget boundary
```

**Dependencies point strictly inward**: UI → Application → Core → Domain.
The Domain layer has zero dependencies. The Core layer uses Qt only inside `.cpp` files.

## 4. Concurrency Model (✅ Frozen and landed in M2)

```
Task Scheduler
      ↓
Independent thread pools (avoid sharing one QThread across 1,000 images + 8-way compare + pre-decode + thumbnails + Histogram + Difference, which causes chaos):
- Image Decode Pool
- Thumbnail Pool
- Analysis Pool
- IO Pool
```

Goal: saturate a 16-core CPU directly, and never stall the UI.

## 5. Cache Design (✅ Frozen and landed in M2)

At least three levels of memory cache plus a disk cache:

- **Thumbnail Cache** (small images, for lists)
- **Preview Cache** (medium images, for the left-column preview)
- **Viewer Cache** (full-resolution decoded ImageData, LRU)
- **Full Resolution Decode** (on demand, streaming / tiled)

The disk cache uses **SQLite** to record `mtime / size / hash / thumbnail (optional)`.
Reopening a thousand images is nearly instant.

## 6. Evolution Roadmap (Milestones)

| Milestone | Content | Status |
| -------- | ------ | ------ |
| **M0 / M1** | Runnable browser: 3 columns, directory tree, gallery, preview, double-click full image, background thumbnails + disk cache | ✅ Prototype complete |
| **M2** | Extract **Image Core + Task Scheduler + domain/ layer + unified event / command / cache system** | ✅ **Complete; architecture frozen** |
| **M3** | **Analysis Engine panel**: Histogram / RGB Mean / Difference → PSNR / SSIM / Noise visualization | Planned |
| **M4** | Image export / format conversion | Planned |
| **M5** | Performance profiling + memory optimization (wire CacheManager into the real load path, LRU eviction policy, benchmarks) | Planned |
| **M6** | Deepen the SQLite disk cache (connect more cache tiers) | Planned |
| **M7** | CommandPalette / AnalysisPanel integration, file operations (rename / recycle bin) | Planned |

Principle: **after the M2 architecture freeze, implement panels and features incrementally** without breaking frozen interfaces.

## 7. M2 Frozen Infrastructure Inventory

| Component | Files | Notes |
| ------ | ------ | ------ |
| domain/ layer | `src/domain/{Image,Histogram,Selection,CompareSession}.h` | Pure business objects; zero Qt dependency |
| ImageFrame | `src/core/image/ImageFrame.{h,cpp}` | General image carrier (pixels + state + histogram) |
| ImageRepository | `src/core/image/ImageRepository.{h,cpp}` | Image lifecycle abstraction (FileSystem + Decoder + Cache) |
| CacheManager | `src/core/cache/CacheManager.{h,cpp}` | Unified cache scheduling (memory + disk) |
| DiskCache | `src/core/image/DiskCache.{h,cpp}` | SQLite disk cache |
| TaskScheduler | `src/core/scheduler/TaskScheduler.{h,cpp}` | 5 independent pools (Metadata / Decode / Thumbnail / Analysis / IO, mapped by `Priority`) |
| Analyzer | `src/core/analyzer/Analyzer.{h,cpp}` + `HistogramAnalyzer.{h,cpp}` | Pluggable analysis interface |
| EventBus | `src/core/EventBus.{h,cpp}` | 4-domain isolated event bus |
| CommandRegistry | `src/core/command/CommandRegistry.{h,cpp}` + 5 seed commands | Command-system infrastructure (CommandPalette UI deferred to M7) |
| ADR documents | `docs/adr/001` – `010` | Architecture decision records |

## 8. Detailed Architecture Specifications

The English specification documents in this directory are the detailed authoritative reference for M2:

- `vision.md` — product vision and scope
- `roadmap.md` — evolution roadmap and milestone plan
- `architecture.md` (this file) — high-level positioning and frozen architecture overview
- `coding_style.md` — C++20 / Qt coding conventions
- `performance.md` — performance targets and benchmarks
- `image_pipeline.md` — image processing pipeline specification
- `cache.md` — detailed cache system design
- `ui.md` — UI layer specification
- `plugin.md` — Analyzer plugin system specification

## M45 lifecycle contracts

CommandStack owns a short operation mutex for serializing mutations, while its
state mutex protects only history snapshots. `ICommand` execute/undo/redo work
does not run under the state mutex, and change callbacks are copied and invoked
after unlocking it. UI-owned stacks complete worker operations by marshalling a
short `recordExecuted` transition to the UI thread.

File operations use `TaskScheduler` with `FileSystemAdapter` progress/cancel
observers. The UI owns generation/alive guards and never lets a worker callback
touch a widget. Clipboard PNG encoding follows the same worker-to-UI handoff.
