# M44 Release Closure Baseline

Date: 2026-08-15
Status: baseline captured before M44 source changes

## Evidence captured in this run

| Gate | Command | Result |
| --- | --- | --- |
| Full local gate | `powershell -ExecutionPolicy Bypass -File D:\\mviewer\\build.ps1 Test` | **88/88 passed** |
| Golden image | included in full gate | passed |
| Benchmark smoke | included in full gate | passed, 122.90 s |
| Benchmark enforcement | included in full gate | passed, 324.14 s |
| Workflow/Browse/Compare | included in full gate | passed (`workflow_ux_tests`, `browse_acceptance_tests`, `browse_convergence_ui_tests`, `compare_acceptance_tests`) |
| Architecture | ` .\\scripts\\architecture_gate.ps1` | 4 advisory warnings |
| Complexity | ` .\\scripts\\complexity_gate.ps1` | 58 hard-limit violations, 6 cyclomatic failures, 44 long functions, 122 warnings |
| Health | ` .\\scripts\\health_score.ps1` | 78.2 / 100, grade C |

The three script commands were invoked directly from PowerShell because invoking
them through a nested `powershell -File` in this managed environment leaves
`$PSScriptRoot` empty and fails before the gate logic runs. That wrapper issue is
environmental and was not changed.

## Architecture baseline

The architecture gate reported four advisory violations:

- `src/previewpanel.cpp` directly includes `core/cache/CacheManager.h` (R1).
- `src/imageviewer.h`, `src/imageviewer_paint.cpp`, and
  `src/imageviewer_contextmenu.cpp` directly include
  `core/image/ImageRepository.h` (R2).

## Complexity baseline

The hard-limit population is 58. The largest reported production files include
`src/analysispanel.cpp` (1379 lines), `src/imageviewer.cpp` (1049),
`src/core/export/ExportJob.cpp` (968), and
`src/core/image/ImageRepository.cpp` (933). The largest test files include
`src/test_workflow_ux.cpp` (3126), `src/test_compare_acceptance.cpp` (1332),
and `src/test_analyze_acceptance.cpp` (940).

The six reported cyclomatic failures are retained as the M44 refactor starting
set: `RawMetadata.cpp`, `compareworkspace_interact.cpp`,
`mainwindow_commands.cpp`, `imageviewer_contextmenu.cpp`,
`WorkspaceSerializer.cpp`, and `imageviewer.cpp`.

## Native UX boundary

Native Windows GUI interaction, physical ICC profiles, mixed-DPI movement,
multiple real volumes, and long-session perceived smoothness are not observable
in this offscreen environment. They remain **MANUAL PENDING** and are not
represented by the 88/88 offscreen result.

## Baseline integrity

No M44 production source change was made before the full test and gate run.
The repository already contained generated health artifacts after the health
check (`docs/quality/dashboard.md` and `build_health.json`); those are recorded
as generated outputs and are not used as M44 implementation evidence.
