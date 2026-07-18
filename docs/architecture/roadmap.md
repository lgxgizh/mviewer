# Architecture Timeline — why infrastructure arrived earlier than its milestone

**Purpose:** prevent a future Agent from "cleaning up" or deleting infrastructure
that looks out-of-place. This document records *when* and *why* each major
subsystem entered the tree, and explicitly notes the cases where an M8-class
capability was introduced early because M4/M5 depended on it.

> This is the **architecture timeline**, not the milestone roadmap
> (`docs/roadmap.md` is the milestone status table). Read both.

---

## The rule this project follows

```
RFC → Acceptance → Implementation → Review → Merge
```

No subsystem is built speculatively. When a later-milestone capability appears
early, it is because an *earlier* milestone's acceptance criteria could not be
met without it — not because of feature creep.

---

## Subsystem timeline (verified against source on disk, 2026-07-17)

| Subsystem | Where | Introduced (milestone) | Why early |
| ----------- | ------- | ------------------------ | ----------- |
| ImageRepository / DecoderRegistry | `src/core/image/*` | M3 | Core pipeline (M3 P0) |
| ImageFrame / ImageBuffer | `src/core/image/*` | M3 | Single pixel container for all consumers |
| Viewer/FullImage LRU + DiskCache | `src/core/image/*`, `src/core/cache/*` | M3→M5 | Adjacent-image instant switch (M3) + 5-level cache (M5) |
| TaskScheduler (priority pools) | `src/core/scheduler/*` | M2 | Decode off the UI thread (prerequisite for everything) |
| ThumbnailPipeline | `src/core/image/*` | M3 Phase-3 | Non-blocking directory load |
| CompareEngine + 6 controllers | `src/core/compare/*` | M4 | Compare & Analysis maturity |
| PixelController / DifferenceEngine | `src/core/compare/*` | M4→M7 | Pixel probe + diff (M4); matured M7 |
| AnalyzerRegistry + 8 analyzers | `src/core/analyzer/*` | M4 | Unified analysis interface |
| Selection (domain ROI) | `src/domain/Selection.h` | M3 | Analyzers consume Selection, not QRect |
| CommandStack / CommandRegistry | `src/core/command/*` | M4→M8 | Undo/Redo; Crop/Compare/Rotate/OpenDir/Label/Rename/Delete/ToggleHist |
| **EventBus** | `src/core/EventBus.*` | **M8 (early)** | Required so sync/pixel/diff notifications stay decoupled from the Widget (M4 needs it) |
| **JobSystem (Job/JobSystem)** | `src/core/job/*` | **M8 (early)** | Required so Diff/analysis run off the UI thread (M4 Diff-async gap depends on it) |
| **Data Model (Workspace→Folder→ImageSet)** | `src/domain/*`, `ImageRepository::loadWorkspace` | **M8 (early)** | Required so M4 compare sessions and M5 browsing chain share one model |
| **Plugin Registry (E2E `example_analyzer`)** | `src/core/plugin/*`, `test_pluginregistry*` | **M8** | Extensibility; the AnalyzerRegistry contract is exercised through it |
| Render Pipeline foundation (Viewport/TileGrid/TileCache) | `src/core/render/*` | M7 | Tile-based paint seed; full Tile Pipeline deferred post-M8 |

---

## The early-arrival cases (do NOT remove)

### EventBus + JobSystem (M8 infra, needed by M4)

M4's acceptance requires:

- Diff computed off the UI thread → needs **JobSystem** (`src/core/job/Job.h`).
- Sync / pixel / diff notifications decoupled from the Widget → needs
  **EventBus** (`src/core/EventBus.*`).

These were landed as part of the M8 "Job System + Plugin Registry" work but are
**dependencies of M4**, not M4 deliverables. A future Agent must treat them as
shared infrastructure, not as something to prune when "finishing M4."

### Data Model (Workspace→Folder→ImageSet)

The `Workspace`/`Folder`/`ImageSet` domain types and
`ImageRepository::loadWorkspace` underpin both the M4 compare session and the M5
browsing chain. Introduced with M8's data-model work; shared, not M4-specific.

---

## Guardrail for Agents

If you find yourself about to delete or "simplify" any of the following, STOP
and check this timeline first — they are load-bearing for milestones earlier than
their nominal number:

- `src/core/EventBus.*` (needed by M4 notifications)
- `src/core/job/*` (needed by M4 async Diff)
- `src/domain/*` model types (needed by M4 + M5)
- `src/core/plugin/*` (the AnalyzerRegistry contract surface)

---

## Milestone themes (summary; full status in `docs/roadmap.md`)

- **M3** — Core Image Pipeline (load/decode/cache/frame/thumbnail)
- **M4** — Professional Compare & Analysis maturity (sync/blink/diff/pixel/
  selection/analyzer-stability/command-unification)
- **M5** — Scale & Performance (disk persistence, hit-ratio, predictive preload,
  1000-img non-blocking)
- **M6** — Vertical Browsing Chain (product-grade decode/metadata/scheduler)
- **M7** — Stability hardening + Render Pipeline foundation
- **M8** — Feature completion: Crop + Data Model + Job System + Plugin Registry

> Reality note: the roadmap status table marks M3–M8 all `✅ Done`. The
> "early arrival" cases above are exactly why a naive read of the tree ("M4 is
> not done, build it from scratch") is wrong — M4's engine already exists; what
> remained was maturity + the Diff-async gap (now captured in
> `docs/rfc/M4_REVIEW_CHECKLIST.md`).
