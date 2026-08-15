# M41 Storage / Runtime / Test Credibility Closure — 2026-08-15

## Verdict

M41 is **automated-gate complete**. The storage, runtime-path, persistence, and
test-credibility follow-ups identified by M40 are closed. The final registered
CTest matrix contains 88 tests and passed three consecutive full runs at
88/88. This verdict does not substitute for the separate human UX review or
target-hardware sign-off.

## Baseline

The pre-change baseline was captured in
`docs/review/M41_STORAGE_RUNTIME_BASELINE_2026-08-15.md` after a clean Release
build. The first full test run registered 88 tests and passed **82/88** in
624.69 seconds. The six failing suites were `analyze_acceptance_tests`,
`appstate_tests`, `cache_tests`, `repository_tests`, `workflow_ux_tests`, and
`browse_convergence_ui_tests`; failures were concentrated in disk-cache,
QSettings/AppState, recovery/session persistence, and runtime file writes.

The initial literal build command also exposed a duplicate case-variant
`PATH`/`Path` environment issue in the calling PowerShell process. Removing
the duplicate `Path` entry allowed the unchanged `build.ps1` entry point to
complete; no build-system file was modified.

## Implemented closure

### DiskCache

- Non-owner threads now use unique named SQLite connections, while the owner
  thread retains the main connection.
- Connection teardown closes and removes every connection owned by the cache.
- Disk limits are enforced with exact SQLite `LENGTH(data)` byte accounting;
  entry and byte limits are both applied.
- Failed runtime-path or database initialization disables the disk tier safely
  instead of exposing a partially initialized database.
- The cache configuration now wires `CacheConfig::diskCacheSize` to the disk
  byte limit.
- `testDiskCacheThreadAffinityAndStress` exercises eight concurrent writers
  and readers for 1,280 operations, then verifies worker-connection cleanup.

### Runtime paths and persistence

- App data, app config, cache, and temporary fallback locations are resolved
  through one runtime-storage helper. The helper creates and probes writable
  directories and falls back to a per-run temporary tree when required.
- QSettings is explicitly configured as INI data under the runtime config
  directory, avoiding native registry coupling and making tests hermetic.
- AppState, analysis history, sessions, recovery state, compare presets,
  settings exports, and thumbnail files use `QSaveFile` where a rewrite is
  required, with commit failure treated as a failed write.
- Logging, crash reports, thumbnail cache files, and trash staging use the same
  runtime contract and fail safely when no writable location exists.

### Test credibility

- CTest assigns every registered test a unique per-test runtime root and
  isolated `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, and XDG directories.
- Tautological or non-observing assertions found in the touched test paths were
  replaced with checks of real state, including cache stress results, pending
  inspector destruction, metadata-overlay path state, corrupt-sibling
  handling, GPU availability stability, and EventBus residual delivery.
- No test was skipped or made weaker to obtain the final result.

## Verification evidence

Focused post-fix acceptance verification:

| Command | Result | Time |
| --- | ---: | ---: |
| `ctest -R browse_acceptance_tests\|workflow_ux_tests\|compare_acceptance_tests` | 3/3 | 31.34 s |

Full registered CTest matrix:

| Run | Command | Result | Total time |
| ---: | --- | ---: | ---: |
| 1 | `build.ps1 Test` | **88/88** | 665.41 s |
| 2 | `ctest --test-dir build_msvc --output-on-failure -j3` | **88/88** | 688.26 s |
| 3 | `ctest --test-dir build_msvc --output-on-failure -j3` | **88/88** | 679.20 s |
| 4 | `build.ps1 Test` (final post-audit source state) | **88/88** | 672.13 s |
| 5 | `build.ps1 Test` (final runtime-path source state) | **88/88** | 675.68 s |

All five green runs included `golden_image`, `bench_smoke`,
`bench_enforce`, `m27_lifecycle`, `m27_lifetime`, `workflow_ux_tests`,
`browse_acceptance_tests`, `browse_convergence_ui_tests`, and
`compare_acceptance_tests`. `golden_image` and both benchmark gates passed in
each run. The benchmark enforcement durations were 406.63 s, 426.36 s,
420.24 s, 412.93 s, and 414.32 s respectively.

During the first post-fix discovery run, one browse acceptance assertion
incorrectly required a hidden test parent to report visible. That run was
87/88; the assertion was corrected to observe the overlay's requested image
path, the focused suite passed 3/3, and all three subsequent full runs passed
88/88. This failed discovery run is retained as audit context rather than
presented as a green result.

## Remaining sign-off

Automated storage/runtime/test-credibility closure is complete. Per the
project rules, long-session perceived smoothness, animation/zoom feel, and
target-hardware performance remain human UX and hardware sign-off items.
