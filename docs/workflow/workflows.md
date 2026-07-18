# Workflows

## 1. Feature Workflow

```
[Spec/ADR] → [Implementation] → [Build + Test] → [Benchmark] → [Self Review] → [RFC status update] → [PR / commit]
```

1. Update relevant `docs/spec/<Module>.spec.md` with new API contract.
2. If decision affects architecture → write new `docs/adr/NNN-*.md` + update RFC status.
3. Implement in `src/core/<module>/`.
4. Add unit tests in `src/core/test_m3m4m5.cpp`.
5. Run build + all tests (`core_tests.exe`, `test_m3m4m5.exe`, `mviewer_unit_tests.exe`).
6. Run benchmark suite → check `benchmark_results.csv` for regressions.
7. Update RFC status `Draft → Accepted → Implemented`.
8. Self review: verify Qt-free headers, thread safety, error handling.
9. Commit atomically; push via PR.

## 2. Bug Workflow

```
[Reproduce] → [Root-cause] → [Regression test] → [Fix] → [Build + Test] → [Commit]
```

1. Reproduce the bug in isolation (add a failing test first if possible).
2. Identify root cause — fix the cause, not the symptom.
3. Add regression test so the bug cannot re-occur.
4. Verify build + tests green.
5. Commit referencing the bug/RFC.

## 3. Performance Workflow

```
[Profile] → [Baseline] → [Hypothesis] → [Implement] → [Benchmark] → [Accept/Reject] → [Commit if kept]
```

1. Run `benchmark.exe` to capture baseline CSV.
2. Identify hotspot.
3. Hypothesize improvement.
4. Implement.
5. Re-run benchmark; diff against baseline.
6. If >5% improvement → commit. If not → revert.

## 4. Review Workflow

```
[Author Self Review] → [Automated CI: Build + Test + Benchmark + Golden] → [Human Review] → [Merge]
```

Self-review checklist:

- [ ] All headers Qt-free (except intentional boundaries like QtConvert.h)
- [ ] New symbols documented
- [ ] Error paths covered
- [ ] Thread-shared state has synchronization
- [ ] No new heap allocations on hot paths (if avoidable)
- [ ] RFC status bumped (`Draft → Accepted → Implemented`)

CI checklist:

- [ ] Build 0 error, 0 warning on MSVC + Ninja
- [ ] Tests pass: core_tests + test_m3m4m5 + mviewer_unit_tests
- [ ] Benchmark: no regression >10%
- [ ] Golden: all image comparisons pass

## 5. Vision Regression Workflow

```
[Generate golden] → [CI compare on each run] → [Fail if >1% pixels differ by >2/255]
```

1. Author generates golden images via `golden/golden_main.exe`.
2. Commit to `golden/` directory.
3. CI / QA runs `golden_compare.exe` on each CI pass.
4. If any output differs beyond tolerance → investigate, and either fix the bug or update golden (with human approval).
