# M12.1 — User Workflow Acceptance & Gap Analysis

**Status:** DRAFT for review (RFC-first). No code changes until approved.
**Author:** Hermes (commander), grounded in the live `master` tree at `57bab0d`
(M11.3 / P3 tiers + portable zip). This is the M12 entry document requested by
the "Next-Phase Planning Review" — it defines real user workflows, the gaps
that actually exist today, and verifiable acceptance criteria. It deliberately
does **not** implement anything.

> Discipline: claims below are verified against source, not assumed. Where the
> planning review's premise diverges from the tree, it is flagged rather than
> silently adopted.

---

## 0. Relation to the prior review

The earlier "engineering review" was based on a **stale `mviewer-master.zip`**
and asked for work that was already done (AnalyzerRegistry, nightly.yml, Tile
RFC, product_workflow.md, first-thumbnail fix). This M12 planning review is
**current** and its conclusion is endorsed here:

> M1–M11 (RC) are done. Next phase is **Product Beta Hardening (M12)**, not
> more infrastructure. Don't refactor ImageRepository, don't add caches, don't
> introduce Rust, don't touch `build.ps1` / `CMakePresets.json` / `ci.yml`.

Two frozen files remain off-limits per AGENTS.md: `build.ps1`,
`CMakePresets.json`, `ci.yml` (only additive `nightly.yml` allowed).

---

## 1. What already exists (verified, do NOT rebuild)

| Capability | Evidence (live tree) | Notes |
| --- | --- | --- |
| Directory → Thumbnail → Viewer | `DirectoryTree`, `ThumbnailPanel::setDirectory`, `ImageViewer` | Wired; thumbnail pipeline priority-queued |
| Compare (sync zoom/pan/ROI/blink/diff) | `CompareWorkspace` + `CompareEngine`; `compareworkspace.cpp:268` mirrors synchronized ROI | Verified green in `compare_workflow_tests` |
| Analysis (Histogram/RGBMean/PSNR/SSIM/Sharpness/Noise/Entropy) | `src/core/analyzer/` (7 analyzers) + `AnalyzerRegistry` (factory, plugin-safe) | Driven from `AnalysisPanel` |
| Pixel Inspector | `analysispanel.cpp:93` "Pixel Inspector tab (M3 Phase-2)"; `RawImageView::pixelInfo` → `CompareWorkspace::pixelInfo` | Live hovered-pixel RGB readout |
| Export report (JSON/CSV/PNG diff) | `ExportCommand` → `core::buildCompareReport` + `Encoder` | `export_tests` 13/13 |
| Workspace save/load | `WorkspaceSerializer` (round-trip) + `MainWindow` 保存/打开工作区 (commit `6e45b57`) | Saves `.mvws`; reopens gallery root |
| Benchmark tiers | `mviewer_bench --emit-data` + `benchmark/generate_bench_data.ps1` + `benchmark/data/README.md` (commit `57bab0d`) | small/medium/large reproducible on D: |
| Performance targets defined | `docs/performance.md` (first thumbnail <100ms, switch preloaded <16ms, etc.) | M12.2 = measure against these |
| Portable zip | `pack_portable.ps1` → `dist/MViewer-portable-1.0.0-rc.zip` (commit `57bab0d`) | windeployqt + Compress-Archive |

**Conclusion of gap analysis:** the *mechanics* of Scenarios A/B/C below are
largely present. The real, unverified risks are (1) **TIFF on clean Windows**,
(2) **no end-to-end manual workflow validation**, (3) **no `docs/api/`**,
(4) **thread-safety audit not written up**. These are M12's work — not new
features.

---

## 2. The three real-user scenarios (corrected)

### Scenario A — Algorithm-result inspection *(corrected from review)*

**Review premise:** "1000 张 YUV 转 JPEG".
**Tree reality:** MViewer has **no `.yuv` file decoder** (only a `RawImageView`
preview widget, not YUV decode). Adopting the YUV premise would make this a
*new feature* — out of M11.1/M12 scope and contradicting the "don't add formats
casually" freeze. **Correction:** the scenario uses the formats MViewer already
decodes — **JPEG / PNG / TIFF** — which is the realistic algorithm-output case
(model dumps are typically PNG/JPEG).

Flow (as wired today):
```
Open directory (1000 JPEG/PNG)
  → ThumbnailPanel (priority pipeline, UI non-blocking)
  → select two images → CompareWorkspace (sync zoom/pan/ROI/blink/diff)
  → AnalysisPanel: Histogram + Pixel Inspector (hover RGB)
  → ExportCommand → report (.json/.csv) + diff PNG
```
Acceptance:
- No crash across the flow.
- UI never freezes (thumbnail decode is async/off-thread).
- All buttons reachable and functional.
- Histogram + Pixel Inspector show live, correct values.
- Exported report opens and matches the on-screen diff.

### Scenario B — Version comparison (before/after)

Flow: open `before/` and `after/` (or multi-select two dirs) → two viewers →
sync zoom/pan/blink/diff.
Acceptance:
- Both viewers' zoom/pan/ROI stay synchronized (already implemented; needs a
  *manual* confirmation pass).
- Diff view renders without error.
- Blink toggles correctly.

### Scenario C — Workspace persistence

Flow: build a session (dir + compare + ROI + analysis) → Save `project.mvws`
→ close → reopen → restore gallery + compare state + ROI + analysis results.
Acceptance:
- Reopen restores the image list and compare/ROI state.
- Analysis results re-display (or are recomputed deterministically).
- Corrupt/empty `.mvws` is handled gracefully (no crash).

> **Gap note (C):** `WorkspaceSerializer` round-trips the *model* (folders +
> image metadata). It does **not** yet persist *compare ROI coordinates* or
> *analysis result text* into the `.mvws`. That is a concrete M12.1 gap to
> close before Scenario C is fully acceptable — see §4.

---

## 3. Workflow gap list (what blocks "feels smooth")

| # | Gap | Severity | Evidence |
| --- | --- | --- | --- |
| G1 | **TIFF fails on clean Windows** (qtiff/libtiff-6.dll not deployed) | HIGH | `test_m3_pipeline.cpp` SKIPs TIFF when codec absent; `FileSystem` lists `.tif` but decode is plugin-gated |
| G2 | **Workspace doesn't persist Compare ROI / analysis results** | MED | `WorkspaceSerializer` serializes folder/image metadata only |
| G3 | **No end-to-end manual workflow test** (only unit/component tests) | MED | `product_workflow.md` is RFC-level; no scripted UI walkthrough |
| G4 | **YUV scenario mismatch** (review assumed YUV decode) | LOW | No `.yuv` decoder; corrected in §2-A |
| G5 | **No `docs/api/`** for ImageRepository/Decoder/Analyzer/RenderEngine | LOW | `docs/api/` absent |
| G6 | **Thread-safety audit not written** for CacheManager/TaskScheduler/ImageRepository/MemoryTracker | MED | Code present; no audit doc |

G1 is the single highest-leverage gap: it is exactly the Phase-3 installer
concern and it is already *detectable today* via the TIFF SKIP. Closing it
(Phase 3) also unblocks real TIFF users.

---

## 4. Proposed acceptance criteria (M12.1 exit)

1. `docs/acceptance/user_workflow.md` (this doc) approved; roadmap gains an
   **M12 Product Beta Hardening** row.
2. A scripted/manual walkthrough of Scenarios A/B/C exists and passes on a
   **clean Windows VM without any Qt install** (proves G1 closed via installer).
3. Workspace save/load round-trips **compare ROI + analysis results** (closes G2).
4. `docs/api/` has at least ImageRepository, Decoder, Analyzer, RenderEngine.
5. Thread-safety audit doc covers CacheManager / TaskScheduler /
   ImageRepository / MemoryTracker (mutexes, lifetime, shutdown).

---

## 5. Out of scope (per freeze + review)

- ❌ Refactor ImageRepository / CacheManager / RenderEngine.
- ❌ Add a 6th cache layer, Rust, GPU pipeline, plugin-ABI freeze.
- ❌ New image formats (YUV/RAW file decode) — correct the scenario instead.
- ❌ Modify `build.ps1` / `CMakePresets.json` / `ci.yml`.

## 6. Suggested execution order (after review approval)

1. **M12.1** this doc → roadmap M12 row → close G2 (Workspace ROI/analysis
   persistence) + G3 (walkthrough).
2. **M12.2** Performance validation against `performance.md` using the
   `benchmark/data/` tiers (already built): first thumbnail <300ms cold,
   preloaded switch <16ms, RSS <500MB over 10000-img / 30-min scroll.
3. **M12.3** Installer — must deploy `qtiff.dll` + `libtiff-6.dll` + Qt
   plugins so TIFF opens on clean Windows (closes G1).
4. **M12.4** Release automation (tag → build → test → package → upload).
5. **M12.5** Quality audit (Qt-boundary scan, thread-safety doc, `docs/api/`).

---

*Next step per review: do NOT code yet. Approve this RFC, then execute M12.1
(gap-closure is small: G2 Workspace ROI persistence + a walkthrough script +
roadmap row). Everything else is sequenced after.*
