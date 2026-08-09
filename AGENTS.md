# MViewer — Agent Development Rules

## Build System

**Single entry point**: `build.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 [Release|Debug|Test|Clean]
```

**Do not** invoke these directly:

- `vcvars64.bat` / `vcvarsall.bat`
- `cmake`
- `ninja`
- `cl.exe`

Always use `build.ps1` unless you are explicitly debugging the build system
(debugging changes must be reverted before commit).

**NEVER** modify `build.ps1`, `CMakePresets.json`, or `.github/workflows/ci.yml`
unless the user explicitly asks. Agents must not "improve" the build/CI — it
breaks local↔CI parity.

## Local Verify Policy

Build → Test → Commit → Push.

**Never** use CI as the primary build verifier.
Always verify locally first: `.\build.ps1 Test`.

## Code Style

- C++20, 4-space indent, 100-column limit
- Headers must compile standalone (include what you use)
- No Qt types in `domain/` or `core/` headers
- Use `std` types in core; Qt allowed only in UI layer

## Project Architecture

```
UI (Qt Widgets) → Application → Core → Domain
```

- **Domain**: Zero dependencies (pure `std` types, no Qt)
- **Core**: Qt-free headers; `.cpp` internals may use Qt
- **UI**: Qt 6 Widgets boundary only

## Agent Role Division

This project is developed by a single-agent team. OpenCode owns the full loop:
product direction, milestone planning, architecture freezes, implementation,
code review, build+test verification, and commit/push. Every change is
reviewed (self-review of the diff) and verified locally before it lands;
nothing merges that breaks `.\build.ps1 Test`.

- **OpenCode (commander + writer + reviewer)**: implements changes against the
  current ADRs and the frozen architecture, runs the local build and tests to
  verify, reviews the diff before committing, and never lets a change merge
  that breaks `.\build.ps1 Test`. Scope and architecture stay frozen unless
  deliberately changed.
- **UX Review Agent (experience reviewer)**: writes **no code**. Runs
  `docs/beta_checklist.md` end-to-end on every PR and answers only these
  questions: is every button necessary; does every operation match user
  intuition; are there redundant clicks; is any state out of sync; are any
  defaults unreasonable; are there animation / layout / zoom anomalies.
  Verdict is 通过 / 阻塞 (with exact steps to reproduce). A PR that is 阻塞
  does not merge, even if all automated gates are green. The automatable
  subset of the checklist is enforced by `workflow_ux_tests` in
  `.\build.ps1 Test`; the UX Review Agent owns everything a machine cannot
  judge (perceived smoothness, flicker, zoom feel, long-session behavior).

Principles:

- Roadmap and ADRs are the source of truth. A change that needs a missing
  capability or scope expansion is surfaced before implementation; scope is
  never silently expanded.
- No change is merged without local build + test green (see Local Verify Policy).
- Infrastructure/build/CI stay frozen unless explicitly asked to change.

## Strategic Direction (Product-First)

**Positioning:** MViewer is a *Visual Analysis Platform for Image Algorithm
Engineers* (Browse → Compare → Analyze → Report → Export → Workspace), not a
high-performance image browser. Subsequent work must serve professional user
workflows, not add more platform infrastructure.

**Agent work ratio (post v1.0 review):** ~50% product experience (UI/Workflow),
25% core functionality (Compare / Analyze / Export), 15% tests & stability,
10% performance. Do not sink time into architecture refactoring or new
infrastructure while product detail gaps remain.

**Frozen — do not refactor or extend (mature enough):**
`CacheManager`, `Scheduler`, `DecoderRegistry`, Build System, CI, Plugin
Framework, Workspace base architecture, Performance Gate.

**Milestone framing:** M14 Professional Browser · M15 Professional Compare ·
M16 Professional Analysis · M17 Professional Productivity (Batch / Workspace
enhancement / auto-recovery / release installer / crash report / auto-update).

**Post-M22 direction (2026-07 external review):** the project has left the
architecture-exploration phase. Do NOT add new capability categories (AI, more
plugins, more formats, more analyzers). Priorities are convergence +
productization + performance closure, aiming Beta → RC:

1. Keep UI complexity down. Hard guardrails (ADR 014): `mainwindow.cpp`
   < 1000 lines, `compareworkspace.cpp` < 800, `thumbnailpanel.cpp` < 800.
   New MainWindow / CompareWorkspace / ThumbnailPanel code goes into the
   matching responsibility TU (`mainwindow_*.cpp`, `compareworkspace_*.cpp`,
   `thumbnailpanel_*.cpp`), never back into the core file.
2. Quality gates are part of `.\build.ps1 Test`: `golden_image` (PSNR / SSIM /
   pixel-diff vs `golden/`), `bench_smoke`, `bench_enforce`
   (`benchmark/performance_budget.json` + `perf_baseline.json`, ±10% hard
   gate). A change that trips any gate does not merge.
3. Product focus: the Browse → Thumbnail → Select → Compare → Zoom-sync →
   ROI → Export-report flow must stay smooth (1000-image folder < 2 s to first
   thumbnails; 8K first display < 500 ms).

**v0.9 Beta 阶段规则 (2026-07 review):** development is organized around
complete user *workflows*, not individual bugs. The two canonical workflows
(Browse: open dir → navigate → zoom → restore → close; Compare: select two →
compare → switch split/overlay/blink/diff → exit → keep browsing) are encoded
in `src/test_workflow_ux.cpp` (`workflow_ux_tests`, part of the test gate) and
in `docs/beta_checklist.md`. Any change touching these flows must extend the
workflow test first. Feature priority order: (1) Compare-mode polish,
(2) Pixel Inspector, (3) Diff Engine. Ship criteria = "可长期使用的产品",
not "开发中的 Demo".

## Git

- Branch: `master`
- Commit messages: imperative mood, describe what changed
- No commit without local build + test passing

## Documentation

- Update `docs/spec/` for API changes
- Update `docs/adr/` for architectural decisions
- Update `CHANGELOG.md` for user-facing changes
