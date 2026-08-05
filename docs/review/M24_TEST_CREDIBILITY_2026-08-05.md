# M24 Phase 6 — Test Credibility Governance (2026-08-05)

Audit of the test suite's truthfulness, executed during M24. Each item lists
the question, the evidence, and the disposition.

## 1. Registered tests all build and execute (verified)

`ctest -N` lists 65 tests (64 at baseline + `version_consistency`); the full
suite ran green twice this session (see §9). Every `add_test(NAME ... COMMAND
<exe>)` in `src/CMakeLists.txt` resolves to an executable that built
(`build_msvc/bin/<exe>.exe` exists for each; verified via the CTest runs).

## 2. Script-referenced targets exist (verified)

`scripts/product_workflow_gate.ps1` chains 5 executables; all exist and the
gate passed. `scripts/test_matrix.ps1` regenerates `docs/test_matrix.md` from
the tree (writes 76 test sources) — now also emits a CTest inventory
(name / command / RUN_SERIAL / UI-linked) parsed from `src/CMakeLists.txt`.

## 3. Always-skip / never-satisfied conditions (audited, none found)

- `test_assets_acceptance` SKIPs only when `testdata/` is absent (external
  asset dependency — the dir is committed; skip is a documented Nightly guard).
- `test_m3_pipeline` SKIPs only when the qtiff imageformat plugin is not
  deployed; windeployqt ships it in this build (verified: test passed).
- `test_analysis_panel` prints "skipped" for an analyzer whose `create()`
  returns null, inside a passing test — a soft per-analyzer note, not a
  test-level skip.
- No test was observed to skip in the two full runs (0 skipped reported by
  CTest).

## 4. Sleep-based flakiness (audited)

Test sleeps are bounded waits with deadlines (`pump()` loops, `while
(t.elapsed() < X)`), not unbounded `sleep_for` races. Production sleeps
(`BatchProcessor` retry delay, `TaskScheduler` poll) are intentional.
`test_job`/`test_compare` use step-waits with hard timeouts. No unbounded
sleep-based race found. New M24 tests follow the same bounded-pump pattern.

## 5. Environment pollution / inter-test ordering (audited)

- UI tests isolate persisted state: `QStandardPaths::setTestModeEnabled(true)`
  + per-test org/app names + `QSettings().clear()` (pre-existing pattern,
  followed by all M24 tests).
- `RUN_SERIAL` is set on workflow_ux, async_lifetime, browse_acceptance,
  compare_acceptance (bench-scale UI tests) so they cannot contend with
  `bench_*`; the rest are order-independent unit executables.
- Test output goes to `QDir::tempPath()` subdirs with distinct names; no shared
  mutable fixture.

## 6. Golden baseline governance (finding + fix)

- `golden_image` compares 4 synthetic render cases vs `golden/` with
  PSNR ≥ 45 dB / SSIM ≥ 0.99 and PASSES. Thresholds are hard-coded in
  `golden/golden_main.cpp`, not in a config file — acceptable for a committed
  baseline, but see finding below.
- **Dead artifact removed:** `golden/ui/main_window_default.png` was NOT
  referenced by the golden gate (or anything else) — it implied UI-level golden
  coverage that did not exist. Deleted; no coverage was lost.
- Benchmark baseline governance: `benchmark/perf_baseline.json` is only
  consulted under `--regression` (nightly); the hard gate is the absolute
  budget in `performance_budget.json`. B10's code/JSON contradiction (report-
  only vs hard-coded 1000 ms cap) was fixed in Phase 1. Baseline updates still
  require a documented reason (none needed this session — B10 was not
  re-baselined, its gate was aligned with the documented spec).

## 7. Performance thresholds vs hardware (finding, evidence recorded)

- `performance_budget.json` documents CI-vs-dev calibration (B3/B4 raised for
  windows-2022 CI). On the 2-core Xeon VM used this session, all hard gates
  pass; B10 (report-only) measured 1480 ms vs 392.8 ms on the baseline box —
  recorded as hardware capability, not regression (see
  `M24_BASELINE_2026-08-05.md` §4.4).

## 8. Build-configuration finding: exceptions and RTTI are OFF everywhere (P1)

- Evidence: every compile in `build_msvc/build.ninja` uses
  `-std:c++20 -MD /wd4869 /wd4228 -Zc:__cplusplus -permissive- -utf-8` — no
  `/EHsc`, no `/GR`. CMake's MSVC defaults were cleared (`CMAKE_CXX_FLAGS*`
  all empty in the cache), so the whole project builds without exception
  unwind semantics and without RTTI.
- Consequence: 47× C4530 warnings (STL paths in TUs using streams/timers) in
  the clean build; `dynamic_cast` degrades to static_cast (no runtime check);
  the project's CI claim of "zero warnings" is not reproducible locally under
  this flag set. The C#7 exception guards added in Phase 4C match the existing
  codebase idiom (many pre-existing try/catch blocks) and are functional, but
  destructor unwinding in throwing paths is not guaranteed.
- Disposition (per M24 rules — no silent build-config change): recorded for
  the commander. Proposal: add `/EHsc /GR` globally (one line in the frozen
  top-level CMakeLists or toolchain) and re-measure the warning count; CI
  parity must be verified before merging such a change.

## 9. Full-suite evidence this phase

Two full `ctest -j4` runs this session: baseline 64/64 after Phase 1 fixes;
the post-Phase-4 run (65 tests incl. `version_consistency`) — see
`docs/review/M24_BASELINE_2026-08-05.md` §3 for the first, and the Phase 8
checkpoint for the final one.
