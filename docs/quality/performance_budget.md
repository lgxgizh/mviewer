# Performance Budget

## Targets

| Operation | Target | Measurement |
| ----------- | -------- | ------------- |
| Cold start | < 300ms | Process entry to first paint |
| Warm start | < 100ms | Process entry to first paint |
| Open folder (1st thumbnail) | < 100ms | Folder open to first visible thumbnail |
| Switch image (preloaded) | < 30ms | Keypress to full render |
| Decode + display (24MP JPEG) | < 50ms | File read to pixels on screen |
| Thumbnail generation | background only | Never blocks UI |
| UI response | < 16ms | Input to frame render |
| Memory (normal workload) | < 500 MB | Private working set |

## Measurement

- Use `benchmark.exe` for automated benchmarks
- Log p50, p95, p99
- Compare against baseline after each commit
- Fail CI on regression > 10%

## Enforcement

Every PR:

1. Run `benchmark.exe`
2. Compare results against budget
3. Document any deviation
4. Architectural justification required for exceedance

### CPU-aware gate profiles

`mviewer_bench --profile auto` detects logical CPU count. Hosts with up to four
logical cores use the `low-core` profile, which relaxes only CPU-sensitive
latency and thumbnail-throughput limits (`1.25x` latency, `0.80x` throughput).
Memory, cache-hit, event-loop, and stability limits remain unchanged.

Use `--profile base` for an unscaled comparison and `--profile ci` for the
committed nightly baseline. Results JSON records both `hardware_profile` and
`logical_cores`; regression comparison is skipped when the baseline profile does
not match, preventing cross-hardware numbers from being treated as regressions.
`bench_enforce` is `RUN_SERIAL`; `bench_smoke` remains a parallelizable link/run
smoke test.

## Current Baseline

The committed baseline — the only source of current numbers — is
`benchmark/perf_baseline.json` (`schema_version` 2, `hardware_profile` `ci`,
recalibrated 2026-07-28). This document deliberately does not duplicate it.

Measure the current values with:

```
mviewer_bench --profile ci --results results.json
```

`--results` writes one JSON verdict per scenario (`name` / `metric` / `value`);
those measured values are then promoted into `benchmark/perf_baseline.json`
(baseline updates require a documented reason — see
`docs/review/M24_TEST_CREDIBILITY_2026-08-05.md` §6). The nightly regression
check runs `mviewer_bench --enforce --regression --profile ci --budget
benchmark/performance_budget.json` (`.github/workflows/nightly.yml`).
