# M41 Storage / Runtime Baseline — 2026-08-15

## Scope

This is the baseline captured before source changes for M41. The repository was
on `master` with a clean worktree. The required `build.ps1 Clean` command was
run first.

## Build environment note

The first literal `powershell -ExecutionPolicy Bypass -File .\build.ps1
Release` invocation failed during CMake compiler discovery with `No
CMAKE_CXX_COMPILER could be found`. The Codex process inherited duplicate
case variants of `PATH` and `Path`; after removing the duplicate `Path` entry
in the calling PowerShell scope, the same project entry point completed
successfully. `build.ps1` was not modified and CMake/Ninja were not used as
the primary entry point.

## Clean Release baseline

- Command: `Remove-Item Env:Path; .\build.ps1 Release`
- Result: PASS
- Build graph: `570/570` compile/link actions completed
- Toolchain: MSVC 19.44.35228, Qt 6.10.3, Ninja generator selected by
  `build.ps1`
- Existing warnings included MSVC C4530 and C4834; no build error occurred.

## Full CTest baseline

- Command: `Remove-Item Env:Path; .\build.ps1 Test`
- Registered tests: **88**
- Result: **82/88 PASS**
- Total test time: 624.69 seconds
- Performance/image gates: `golden_image` PASS, `bench_smoke` PASS,
  `bench_enforce` PASS (335.51 seconds)

The six failing suites were:

### `analyze_acceptance_tests`

Five C#6 persistence assertions failed at `src/test_analyze_acceptance.cpp`:

- analysis history persisted to disk;
- result reload for `/data/img_a.png`;
- result reload for `/data/img_b.png`;
- pinned state survives restart;
- history survives restart.

### `appstate_tests`

One assertion failed: `AppState::save succeeds`. The in-memory favorites and
recent-file round trips passed.

### `cache_tests`

Five disk-tier assertions failed: entry after put, non-zero disk accounting,
disk get after memory clear, dimensions, and byte identity. The in-memory
cache and LRU assertions passed.

### `repository_tests`

Three disk/cache assertions failed: two predictive-preload memory hits and the
second repository load's disk-cache hit. Fresh decode and ICC sidecar behavior
passed.

### `workflow_ux_tests`

Fourteen assertions failed. The failures covered recovery fixture/prompt
storage, one slideshow advance, legacy sidebar migration, close-time panel
visibility persistence, and cross-restart panel/session restoration. The
compare, viewer, metadata, cancellation, and lifecycle portions passed.

### `browse_convergence_ui_tests`

Thirty-one ThumbnailCache disk assertions failed. In-memory pipeline and
filter behavior passed, while cache put/get, byte accounting, historical file
scan, LRU eviction, and externally-created/overwritten file checks failed.

## Baseline conclusion

The baseline is exactly the M40 headline of **82/88**, with failures
concentrated in runtime storage isolation/persistence and disk-cache behavior.
No failing test was skipped or weakened to obtain this result.
