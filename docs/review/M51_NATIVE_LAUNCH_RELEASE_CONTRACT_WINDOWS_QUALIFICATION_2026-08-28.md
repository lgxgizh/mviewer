# M51 — Native Launch, Release Contract & Windows Qualification Closure

Date: 2026-08-28
Branch: `master`
Target patch line: `1.0.13`

## Scope

M51 closes the release-candidate seams left after the M50 workflow
convergence:

- one external-open contract for command line, shell associations, and
  drag-and-drop;
- native Windows workspace/project associations and a versioned installer;
- a strict portable/installer artifact contract that exercises the production
  payload;
- always-on Windows crash diagnostics with a real child-process smoke test;
- Unicode, mixed-target, adversarial, and repeated RC-soak evidence.

## Baseline and implementation

The pre-M51 characterization baseline was a clean `build.ps1 Test` run with
107/107 tests passing. The release path had no single classifier shared by
startup and UI entry points, installer version/association rules were not a
strict contract, and the packaged self-test was not a reliable production
payload gate.

## Confirmed bugs and root causes

| Reproduction | Root cause | M51 closure |
|---|---|---|
| A directory, `.mvws`, or `.mvproj` launched through an external path was routed through the image path | startup and UI entry points had separate file/image assumptions and `setOpenOnLaunch()` always called the image workflow | one application-layer classifier and dispatcher |
| Unsupported or mixed dropped targets could be treated as image/Compare input | drop handling checked only directory-ness and did not share the supported-image contract | atomic plan validation before any UI mutation |
| Installer associations described a legacy `.mviewer` format and could carry default version metadata | NSIS rules and current workspace/project extensions had drifted; `VI_VERSION` had a silent fallback | `.mvws`/`.mvproj` associations plus required four-part version input |
| A package could be assembled without proving its actual runtime payload | packaging and release validation were separate best-effort paths | strict gate expands the ZIP, launches its executable, and checks dependencies, PE metadata, and checksums |
| Crash-path setup depended on work that was unsafe during an exception | crash directory/filter setup and handler operations used too much high-level runtime state | pre-created directory, fixed-buffer Win32 path, and recursion guard |

### Unified native launch

`application/ExternalOpen` now normalizes one outer quote pair, classifies
existing paths as supported images, directories, `.mvws`, or `.mvproj`, and
returns stable errors for missing/unsupported targets. Multiple targets are
accepted only when every target is an existing supported image; mixed or
unsupported batches are rejected atomically. `main.cpp`, MainWindow startup,
and drag-and-drop all use the same plan/dispatch boundary, after plugins have
been loaded.

The command-line and production navigation regressions cover spaces, Unicode,
emoji, uppercase image suffixes, quoted paths, directories, workspace/project
files, missing paths, unsupported files, mixed batches, and the final
MainWindow dispatch. Windows drive/UNC switch-vs-path behavior remains
explicit in the lexical parser.

| External target | Automated result | Route |
|---|---|---|
| One supported image | PASS | Viewer |
| Two or more supported images | PASS | Compare |
| One directory | PASS | Browse directory |
| One `.mvws` | PASS by restore contract tests; dispatcher contract is shared | `openWorkspaceFile()` |
| One `.mvproj` | PASS by restore contract tests; dispatcher contract is shared | `openProjectFile()` |
| Unsupported, missing, or mixed target list | PASS | stable error; live session unchanged |

### Release contract and installer

- `scripts/release_version.ps1` is the version identity source for package
  validation and derives `1.0.13.0` Windows metadata from `1.0.13`.
- `.mvws` and `.mvproj` are registered with quoted `"%1"` shell commands;
  the obsolete `.mviewer` association is not registered.
- The portable payload has exactly 45 files and includes the application,
  `mviewer_core.dll`, the required Qt Core/Gui/Widgets/Sql runtime, Windows
  platform support, SQLite support, and the MSVC CRT. Development files,
  project files, test/benchmark executables, and PDBs are rejected.
- The strict gate starts the extracted production `MViewer.exe --selftest`
  with the shipping `qwindows.dll` platform plugin. The self-test reports
  4/4 checks passed. Installer `FileVersion` and `ProductVersion` are both
  `1.0.13.0`.
- Exact artifacts were generated and checksummed:
  `MViewer-1.0.13-portable.zip` (27,548,494 bytes) and
  `MViewer-1.0.13-Setup.exe` (27,591,999 bytes). The strict release contract
  passed, including SHA256SUMS verification. The final manifest records
  portable SHA256 `4214f69c0635ff5947d4f8874d3913978b4c195a64ceb24674b32ccd0d10bf1f`
  and installer SHA256
  `f2e791882cd43ab5c6bb7d7018cbf0fc298366b0ecc099f23394fc15a94e7287`.

| Installer contract | Result |
|---|---|
| `.mvws` ProgID and OpenWith | PASS — `MViewer.Workspace` |
| `.mvproj` ProgID and OpenWith | PASS — `MViewer.Project` |
| Quoted executable and `%1` argument | PASS |
| Legacy `.mviewer` registration | PASS — not registered; uninstall removes stale legacy key |
| FileVersion/ProductVersion | PASS — `1.0.13.0` |

### Crash diagnostics

The Windows handler creates the crash-report directory before installing the
SEH filter, uses a recursion guard and fixed-buffer Win32/DbgHelp operations,
and writes timestamp/PID/TID `.dmp` and `.txt` artifacts under the application
data crash-report directory. `crashhandler_tests` launches a crashing child,
checks abnormal termination and non-empty dump/text artifacts, then removes
only those artifacts created by the test.

## Sidecar, navigation, and soak evidence

The inherited M50 navigation suite remains green for latest-wins Sidecar
completion, active filters, Unicode directory paths, Back/Forward, teardown,
and stale-completion protection. M51 adds the production external-open image,
directory, mixed-target, and session-preservation checks to that suite. The
`m51_rc_soak` CTest entry runs the real M46 workflow workload for 20 iterations;
it passed with scheduler convergence and no crash/deadlock report. The full
benchmark enforcement also passed its boundedness/workflow resource checks.

## Unicode, long-path, and UNC evidence

Automated fixtures cover ASCII, spaces, Chinese, emoji, quoted paths, Unicode
workspace/project names, and uppercase image suffixes. The local Windows
environment did not provide a clean isolated UNC share or a separately
configured long-path target policy, so UNC, >260-character paths, removable
volumes, and network interruption remain MANUAL/BLOCKED below. No green result
is claimed for those target-dependent rows.

## Automated evidence

| Check | Result | Evidence |
|---|---|---|
| Pre-M51 characterization baseline | PASS — 107/107 | clean baseline run |
| Final clean build + test gate | PASS — 110/110; 710.96 s | `build.ps1 Clean`, then `build.ps1 Test` |
| Final no-source-change verification | PASS — 110/110; 684.71 s | second `build.ps1 Test` |
| M51 repeated RC soak | PASS | `m51_rc_soak` (20 iterations) |
| External-open parser and target contract | PASS | `commandline_tests` |
| Production external-open dispatch/navigation | PASS | `m50_navigation_tests` |
| Crash dump child-process smoke | PASS | `crashhandler_tests` |
| Version identity and installer contract | PASS | `release_version_tests`, `release_contract_tests` |
| Canonical workflow and UI gates | PASS | `workflow_ux_tests`, `product_workflow_gate`, `browse_convergence_ui_tests` |
| Architecture regression | PASS — 0 warnings | `architecture_gate_regression` |
| Complexity evidence | PASS — no new hard regression; advisory baseline remains 3 hard findings / 105 warnings | `complexity_gate.ps1 -Json`, compared with the pre-M51 tree |
| Golden image and performance gates | PASS | `golden_image`, `bench_smoke`, `bench_enforce` |
| Strict release package | PASS | `package_release.ps1`, `release_contract_gate.ps1` |

The final full gate includes all 110 registered tests. The package was rebuilt
from the clean Release output after the final source changes; the strict gate
ran against the extracted ZIP payload and verified the installer metadata and
checksum manifest.

## Quality evidence freshness

After the final source changes, the following generated evidence was refreshed
from the current tree: `build_health.json`, `docs/quality/dashboard.md`, and
`docs/test_matrix.md` (106 test sources). The health snapshot reports 100 for
build, tests, architecture, and regression, with the existing advisory
complexity findings described above; no scoring formula was changed. The
strict RC command also generated `release_checklist_report.md` with overall
`PASS` (the report is a local generated artifact ignored by the repository).

## Native Windows qualification boundary

This repository run is native MSVC/Windows automation, but it is not a clean
end-user target-machine sign-off. The following rows remain **MANUAL / BLOCKED**
and are not represented as automated PASS:

- install, upgrade, Explorer Open With handoff, association launch, and clean
  uninstall on a physical clean Windows machine;
- long-path policy, UNC/network shares, removable volumes, and interruption or
  reconnection during Sidecar/import/export;
- real multi-monitor DPI changes, fractional scaling, GPU/driver combinations,
  ICC/color-managed display behavior, and native dialog behavior;
- extended human GUI soak covering flicker, zoom feel, focus continuity,
  stale-state perception, and resource behavior over a long session.

The target-machine pass should install both artifacts, exercise each native
association with paths containing spaces and Unicode, repeat Browse → Select →
Compare → Zoom/ROI → Export, run the long-path/UNC/network matrix, and verify
that uninstall removes the current associations without leaving the obsolete
`.mviewer` registration. It should record OS build, display scaling, GPU,
driver, ICC profile, storage type, and exact artifact hashes.

## Files changed

The implementation is limited to the M51 boundary: application external-open
contract and dispatch (`src/application/ExternalOpen.*`, command-line and
MainWindow files), targeted parser/production regression tests, crash-handler
hardening/tests, release version/manifest/package/checklist scripts, the NSIS
installer, the release workflow, and current-state/release evidence docs. The
frozen `build.ps1`, `CMakePresets.json`, and `.github/workflows/ci.yml` were not
modified.

## Known limitations

The package gate validates the unpacked production payload and native Windows
platform plugin, but it cannot replace physical Explorer association,
install/uninstall, mixed-DPI, GPU/ICC, long-path/UNC, and human GUI UX review.
Those limitations are release qualification rows, not hidden automated
failures.

## Release verdict

M51 automated closure is complete for patch line `1.0.13`: the clean build/test
gate passed twice, the RC soak and native-launch regressions are green, and the
portable ZIP plus NSIS installer passed the strict production artifact
contract. Physical target-machine qualification remains a separate
MANUAL/BLOCKED sign-off as listed above.
