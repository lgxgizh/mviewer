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

This project is developed by a two-agent team. The division is by *role*, not
by task — the commander does not write the bulk of the implementation, and the
writer does not decide scope or merge.

- **Hermes (commander / reviewer)**: owns product direction, milestone
  planning (roadmap M3/M4/M5 with acceptance criteria), architecture freezes,
  code review, build+test verification, and commit/push. Delegates implementation
  to OpenCode; reviews the diff before it lands; never lets a change merge that
  breaks `.\build.ps1 Test`.
- **OpenCode (code writer)**: implements the specific change Hermes delegates,
  against the current ADRs and the frozen architecture. Writes code, runs the
  local build to confirm it compiles, but does **not** commit or push, and does
  **not** change scope/architecture on its own.

Principles:

- Roadmap and ADRs are the source of truth. A writer that finds a missing
  capability must surface it to the commander; it must not silently expand scope.
- No change is merged without local build + test green (see Local Verify Policy).
- Infrastructure/build/CI stay frozen unless the commander explicitly asks.

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

## Git

- Branch: `master`
- Commit messages: imperative mood, describe what changed
- No commit without local build + test passing

## Documentation

- Update `docs/spec/` for API changes
- Update `docs/adr/` for architectural decisions
- Update `CHANGELOG.md` for user-facing changes
