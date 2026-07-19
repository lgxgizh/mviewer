# RFC — Product Beta: from infrastructure to value

**Status:** DRAFT for review (RFC-first). No code until approved.
**Author:** Hermes (commander), grounded in the live `master` tree at `19f9c55`.
**Date:** 2026-07-19

> Discipline: claims below are verified against source, not assumed. Where the
> proposal collides with an existing decision, it is flagged rather than silently
> adopted.

---

## 0. Context — what actually exists today (verified)

The "engineering review" premise (infrastructure incomplete) is **stale**. The
tree already has:

- CI: `ci.yml` (Phase-1 gate) + `nightly.yml` (clang-tidy/ASan/benchmark,
  non-gating) + `release.yml` (tag→build→test→package→GitHub Release).
- Quality gates: `.clang-format`, `.clang-tidy`, `.pre-commit-config.yaml`,
  `QUALITY.md`.
- Architecture/design docs: `docs/architecture/`, `docs/design/`, `docs/spec/`,
  10 ADRs (`docs/adr/001`–`010`), `docs/api/` (4 pages).
- Acceptance: `docs/acceptance/` (user_workflow, product_workflow,
  workflow_walkthrough, M12.3_installer, M12.5_quality_audit).
- Benchmark: `benchmark/` harness + `mviewer_bench`; tiers `small`(100)/
  `medium`(1000) generated on D:; `M12.2_results.md` has real p50/p95/p99.
- Installer: `installer/mviewer.nsi` + `pack_installer.ps1`; G1 (TIFF on clean
  Windows) **runtime-proven** via `scripts/g1_clean_windows_proof.ps1`.
- Plugins: `example_analyzer.dll` + `PluginLoader`/`PluginManager` + ADR-005
  (plugin analysis). The Plugin SDK **already exists minimally**.
- Perfetto: `MemoryTracker` already has an opt-in Perfetto trace shim (M7).

**Conclusion:** infrastructure is done. The next phase is **Product Beta** —
extract value from this scaffolding, not add more of it.

---

## 1. Operating principle — subtraction over addition

Every proposed change must answer three questions; if **two** are "no", defer it:

1. Will a real user actually use this?
2. Can an existing module be reused instead of adding a layer?
3. Does it make the software faster / more stable / easier to maintain?

**Scope of the rule:** the *subtraction* discipline applies hardest to
`core/`/`domain/` code (no new caches, no scheduler rewrite, no RenderEngine
rewrite). The eight phases below are mostly **test / docs / CI / data** — these
are net-positive and low-risk, and are explicitly *exempt* from the "don't add"
rule (they add leverage, not complexity). The rule's job is to stop *core*
creep, not to block the dashboard.

**Forbidden (carried from AGENTS.md + review):**
- ❌ Refactor `ImageRepository` / `CacheManager` / `RenderEngine`
- ❌ Modify `build.ps1` / `CMakePresets.json` / `ci.yml`
- ❌ Introduce Rust, a 6th cache layer, GPU/Vulkan code now
- ❌ Rewrite `TaskScheduler` with coroutines
- ❌ Plugin ABI freeze change

---

## 2. Public versioning — rename proposal (with collision flag)

You proposed: stop calling them M13/M14… and use **Beta → RC → 1.0 → 1.1 →
2.0**. **Collision:** M11 already shipped a GitHub pre-release tagged
`v1.0.0-rc` (see `docs/release/RELEASE_v1.0.0-rc.md`). So "1.0" and "RC" are
already consumed by a pre-M12 state.

**Proposal:**
- Internal milestone tags stay (M12 done; new work is **M13 = Product Beta**)
  so the roadmap history stays coherent.
- The *public* track is relabeled: **Beta (current M13 work) → 1.0 (final,
  first non-prerelease) → 1.1 → 2.0**. The old `v1.0.0-rc` is acknowledged as
  the "internal RC"; the real **1.0** is what this plan produces.
- This avoids double-booking "1.0" while honoring your product-management intent.

*Open question for approval:* accept the "Beta/1.0/1.1/2.0" public relabel with
M13 = Beta, or keep numbering M13…M18? (I recommend the relabel; it matches
your intent and the old RC is clearly pre-final.)

---

## 3. The eight phases (M13 = Product Beta)

Executed **in order**; each phase has an acceptance gate that must be green
before the next starts (your "sequential milestones" rule).

### Phase 1 — Product Workflow automated verification ★★★★★
**Goal:** any PR cannot silently break Browse / Compare / Workspace / Export.
**Deliverable:** `docs/workflow/{browse,compare,export,workspace}.md`, each with
*user action / expected result / automated test / manual test*. Plus a CI job
(or `ctest` suite) that exercises the full chain:
`Open Folder → Thumbnail → Viewer → Compare → Analyzer → Export → Workspace →
Restart → Restore`.
**Reuse:** existing `compare_workflow_tests` / `analysis_panel_tests` /
`workspace_persist_tests` are the building blocks — extend, don't rewrite.
**Acceptance:** 100% of the documented workflow runs green (automated) + a
manual walkthrough checklist signed off.
**Subtraction check:** this *documents + wires existing tests*, no new core code.

### Phase 2 — Benchmark Dashboard ★★★★★
**Goal:** answer "did performance drop in the last 3 months?"
**Deliverable:** `benchmark/report/` with `index.html` + `history.csv` +
`plot.py` (or a tiny static generator). Each `mviewer_bench` run appends a row
(Folder Scan / First Thumbnail / Decode p50-p95-p99 / Memory / FPS) and
regenerates the HTML trend.
**Reuse:** `M12.2_results.md` already has the metric definitions; `nightly.yml`
already runs `--enforce` — point it at the report generator.
**Acceptance:** a commit to history.csv + a rendered trend chart exists; can
diff two dates.
**Subtraction check:** additive script only; no core change.

### Phase 3 — Release Pipeline completion ★★★★★
**Goal:** `git tag` → Release with zip + installer + PDB symbols + test report,
downloadable and runnable on clean Windows.
**Deliverable:** extend `release.yml` to also upload PDB symbols (`build_msvc`
already emits `.pdb` via `/Zi`); `packaging/windows/{portable,installer}/`
layout; `test_package.ps1` already gates G1. NSIS `.exe` needs `choco install
nsis` one-time (deferred this whole time — do it here).
**Acceptance:** a tagged release yields portable zip + installer exe + symbols
+ benchmark/test report artifacts; clean-VM run confirmed.
**Subtraction check:** wiring existing scripts; one-time NSIS install.

### Phase 4 — Real image dataset ★★★★☆
**Goal:** stop testing on fake PNG/JPEG; use real camera files.
**Deliverable:** `test_assets/{camera,iphone,pixel,nikon,sony,canon,drone}/`
covering JPEG / PNG / TIFF / 16-bit TIFF / Gray / RGBA / CMYK / Large / Corrupted
/ bad-EXIF / bad-ICC. Sourced from real files (you provide or we fetch
CC-licensed samples).
**Acceptance:** every asset Opens OK / Compares OK / Exports OK / No Crash.
**Subtraction check:** data only; no core code (Decoder already handles these
formats — this *proves* it).

### Phase 5 — Perfetto profiling ★★★★☆
**Goal:** stop guessing; measure Scan/Decode/Thumbnail/Render/Upload/Compare.
**Deliverable:** wire `MemoryTracker`'s existing Perfetto shim to a trace; add a
`--trace` mode to `mviewer_bench`; produce CPU timeline + memory timeline.
**Acceptance:** each pipeline stage has p50/p95/p99 from a real trace (not
inference). Optimization direction decided *from data*, not experience.
**Subtraction check:** enables future subtraction (find dead time); no core
rewrite now.

### Phase 6 — Plugin SDK stabilization ★★★★☆
**Goal:** third party can `git clone → compile → drop dll → recognized`.
**Deliverable:** document the *existing* `PluginLoader`/`PluginManager`/`example_analyzer`
as the SDK; ship a minimal demo plugin build; clarify the ABI contract (ADR-005).
**Acceptance:** a fresh checkout of the demo plugin compiles against the SDK
headers and is loaded by MViewer.
**Subtraction check:** document + 1 demo, no new analyzer types.

### Phase 7 — GPU route RFC ★★★☆☆
**Goal:** design only, no code. `RFC: CPU → Tile → GPU Upload → Direct2D →
D3D11 → Vulkan(future)`.
**Deliverable:** `docs/rfc/M13_GPU_ROADMAP.md`.
**Acceptance:** RFC approved; no implementation.

### Phase 8 — Long-term public roadmap
Adopt the Beta/1.0/1.1/2.0 public track (see §2). Update `roadmap.md` to show
the product timeline, not just internal milestones.

---

## 4. Execution order & gates

1 → 2 → 3 → 4 → 5 → 6 → 7 → 8, **strictly sequential**. Each phase:
- `build.ps1 Release` green
- `build.ps1 Test` green (26/27; `bench_smoke` is the known Windows teardown,
  ALL PASS on direct run — environmental, not a logic regression)
- Benchmark smoke green
- Phase acceptance signed off
- Commit carries: change note + test result + perf data + acceptance result

---

## 5. What this RFC deliberately does NOT do

- No new `core/` modules, no cache/scheduler/render rewrite.
- No Rust, no GPU code (Phase 7 is RFC-only).
- No modification of frozen files (`build.ps1`/`CMakePresets.json`/`ci.yml`).
- No "infrastructure for its own sake" — every phase yields user/operator value.

---

## 6. Open questions for approval

1. **Versioning (§2):** accept M13=Beta + public relabel Beta/1.0/1.1/2.0
   (old `v1.0.0-rc` = internal pre-release), or keep M13…M18 numbering?
2. **Phase 4 data:** will you provide real camera files, or should I fetch
   CC-licensed samples (e.g. public test-image repos) and catalog them?
3. **Phase 3 NSIS:** approve a one-time `choco install nsis` on this box to
   finally produce the `.exe`?
4. **Start point:** approve Phase 1 (workflow docs + CI gate) as the first
   executable item?

*Next step: approve this RFC (resolve the 4 questions), then execute Phase 1.*
