# M52 — Release Quality Truthfulness & Workflow Boundary Convergence

Date: 2026-08-28  
Branch: `master`  
Baseline commit: `c1b1412`

## Decision

M52 closes the release-evidence and workflow-boundary gaps found in the phase-0
review. The implementation keeps the existing UI/Application/Core/Domain
architecture and makes the automated result legible: a hard-gate PASS means
zero measured hard failures; advisory warnings are retained as accepted
baseline debt and are not relabeled as a clean codebase.

Native target-machine qualification remains a separate boundary. Physical
Windows DPI/ICC/UNC/installer behavior, long-session GUI feel, and extended
soak are MANUAL/BLOCKED when that target is unavailable; offscreen tests do not
claim those rows.

## Phase 0 baseline

The mandated baseline command was run before implementation:

```text
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

Observed baseline: **110/110 CTest passed**, total test time **682.56 s**.

| Check | Baseline fact |
| --- | --- |
| `build_health` | Overall 84.5, grade B; Complexity score 15 |
| `architecture_gate` | PASS, 0 violations |
| `complexity_gate` | 3 hard findings, 105 warnings |
| `sourceDisplayRequest` | 126-line span, CC 27 |
| Responsibility/file findings | `test_m48_phase0.cpp` 887 lines; `mainwindow_session.cpp` 1082; `mainwindow_ui_layout.cpp` 893 |
| Encoding | Generated dashboard/matrix contained `鈥?` / `路` mojibake; relative PS5.1 script invocation could lose `$PSScriptRoot` |

The baseline details and measured command output are preserved in
[`M52_PHASE0_BASELINE_2026-08-28.md`](M52_PHASE0_BASELINE_2026-08-28.md).

## Implementation and regression evidence

### Script and release-evidence truthfulness

- `test_matrix.ps1` and `health_score.ps1` now work through the Windows
  PowerShell 5.1 entry point with an explicit repository root fallback.
- Generated Markdown/JSON is written as UTF-8 without a BOM and runtime
  generated punctuation is ASCII-safe for non-UTF-8 host output.
- `script_encoding_regression` invokes the real scripts, performs strict UTF-8
  decode/encode round trips, checks common mojibake markers, and checks a
  non-ASCII/surrogate-pair sentinel.
- Corrupted mojibake was removed from production source comments.

### Complexity honesty

- Split `mainwindow_*`, `compareworkspace_*`, and `thumbnailpanel_*` TUs use an
  800-line warning and 1000-line hard ceiling; the old broad 1500/2500-line
  responsibility exemption is gone.
- Production functions over 120 lines or CC over 25 remain hard failures.
- `complexity_gate_regression` demonstrates that a 1001-line responsibility TU
  fails, while two legal sub-1000-line TUs pass; it also asserts the real tree
  has zero hard failures.
- `mainwindow_session.cpp` is now below the hard ceiling after notification
  methods moved to `mainwindow_session_notifications.cpp`.
- M48 failure cases moved out of the 800-line test TU into
  `test_m48_phase0_failures.cpp` without deleting coverage.

### Source-backed Compare planning

`CompareWorkspace::sourceDisplayRequest` now collects live widget/transform
state and delegates the decision to the Qt-free value component
`compareworkspace_display_planner.{h,cpp}`. The table-driven planner regression
covers placeholder, Fit, independent/uniform scales, first/moderate/high zoom,
pan edge, near-full coverage, DPR 1/1.25/1.5/2, stale fit state, crop,
rotation, resize, large JPEG, duplicate panes, and A→B→A replacement.

### Real workflow boundary

The production `m50_navigation_tests` path now covers Unicode directories,
rapid A/B/C latest-intent navigation, a source disappearing during the
transition, missing/unsupported external targets preserving the live session,
real Compare entry, ROI retention, rapid pane replacement, and Decode,
Thumbnail, Metadata, Analysis, and I/O pool drainage after close.

## Product truthfulness

- **Implemented:** 100 MP JPEG source-backed display through the Native LOD
  path, including Compare materialization and bounded zoom regions.
- **Not equivalent:** large TIFF has no JPEG-equivalent native LOD path in this
  release; its fallback behavior remains explicitly documented rather than
  represented as a JPEG-class success.
- **Automated PASS:** zero hard complexity findings, zero architecture
  violations, script encoding regression green, workflow regressions green,
  and the canonical test gate green.
- **Accepted baseline debt:** advisory complexity warnings remain visible in
  the generated dashboard and test evidence. They are maintenance feedback,
  not hidden whitelist exceptions.

## Qualification record

Focused checks completed during implementation:

- `script_encoding_test.ps1`: PASS (Windows PowerShell entry points, strict
  UTF-8 and mojibake checks).
- `complexity_gate_test.ps1`: PASS (illegal responsibility TU fails; legal split
  passes; real tree hard failures 0).
- `compare_display_planner_tests`: PASS (all table cases).
- `m50_navigation_tests`: PASS (all adversarial workflow checks).
- `m48_phase0_failure_regressions`: PASS.
- Release build: PASS via `build.ps1 Release`.

The final canonical qualification runs are recorded below after the final
source-stable gate is complete:

- `build.ps1 Test` run A: **PASS — 114/114**, total **694.59 s**.
- `build.ps1 Test` run B: **PASS — 114/114**, total **690.70 s**.
- `m51_rc_soak`: **PASS** in both runs (37.28 s and 37.37 s).
- `build_health`: Overall **92.7**, grade **A**; Complexity score **60**;
  complexity hard failures **0**, advisory warnings **107**.
- Extended physical/native GUI soak: **MANUAL/BLOCKED** unless run on target
  Windows hardware
