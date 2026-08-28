# M52 — Phase 0 Characterization Baseline

Date: 2026-08-28  
Branch: `master`  
Commit: `c1b1412` (`v1.0.13`)

## Scope and method

This baseline was recorded before M52 implementation work. The repository was
clean at the start of the run. Verification used the mandated entry point:

```text
powershell -ExecutionPolicy Bypass -File .\build.ps1 Test
```

The run completed with **110/110 tests passing**, 0 failures, in **682.56 s**.
It included the benchmark, golden-image, workflow, architecture, complexity,
M47 source-backed display, M50 navigation, and M51 release/lifecycle gates.

## Gate snapshot

| Evidence | Result |
|---|---|
| Full `build.ps1 Test` | PASS — 110/110 |
| Architecture gate | PASS — 0 warnings/violations |
| Complexity gate | FAIL/advisory — 3 hard findings, 105 warnings |
| Complexity cyclomatic hard failures | 1 |
| Complexity function-length hard failures | 1 |
| Test-source inventory | 106 sources |
| Health score regenerated from current tree | 84.5, grade B |

The health snapshot's complexity dimension was 15 because the quality report
still represents the three hard findings. `build_health.json` was stale with
respect to the current commit before this baseline; the live Phase-0 generator
reported commit `c1b1412`.

## Reproduced findings

The current complexity report reproduces all known M52 debt:

1. `src/compareworkspace_render.cpp::sourceDisplayRequest()` is 126 lines
   with approximate cyclomatic complexity 27, exceeding the production hard
   limits of 120 and 25.
2. `src/test_m48_phase0.cpp` is 887 lines, exceeding the ordinary 800-line
   test-file cap.
3. Responsibility translation units use the broad 1500/2500-line exemption:
   `mainwindow_session.cpp` is 1082 lines and `mainwindow_ui_layout.cpp` is
   893 lines. The current gate does not give those files useful feedback.

The current source inventory also records the related responsibility files:

| Area | Current files of note |
|---|---|
| MainWindow | `mainwindow_session.cpp` 1082 lines; `mainwindow_ui_layout.cpp` 893 lines |
| Compare | `compareworkspace.cpp` 643 lines; `compareworkspace_render.cpp` 576 lines |
| Thumbnail | `thumbnailpanel_fileops.cpp` 737 lines |

The generated `docs/quality/dashboard.md` and `docs/test_matrix.md` contain
PowerShell-generated mojibake such as `鈥?` and `路`. Re-running the matrix
generator in the current host produced 106 sources with valid Unicode, so the
defect is in the Windows PowerShell 5.1 execution/encoding boundary rather
than in the requested document semantics.

Direct Windows PowerShell 5.1 invocation using a relative `-File` path also
reproduced an empty `$PSScriptRoot`, causing the scripts' default repository
path expression to fail. An absolute script path plus `-Repo D:\mviewer`
works; M52 must make the normal project invocation self-contained.

## Baseline interpretation

The product and automated workflow gates are green, but the quality evidence
is not yet truthful: the complexity gate still reports hard debt and generated
documentation can be corrupted by the supported Windows shell. M52 therefore
focuses on converging the evidence and boundaries without changing the frozen
build/CI infrastructure or adding a new product capability category.

Physical Windows qualification remains outside this baseline: ICC hardware
behavior, mixed-DPI, real UNC/network/removable volumes, Explorer association,
clean install/uninstall, physical GPU/driver combinations, and multi-hour
human UX remain MANUAL/BLOCKED unless exercised on the target machine.
