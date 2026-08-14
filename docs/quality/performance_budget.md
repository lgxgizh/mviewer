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

## Current Baseline (M5)

| Operation | Avg | Status |
| ----------- | ----- | -------- |
| Decode 1920x1080 (JPEG) | 24.7ms | ✅ |
| Encode 1920x1080 (JPEG) | 64.0ms | ✅ |
| ImageCache hit | <0.01ms | ✅ |
| CacheManager miss | 0.2ms | ✅ |
| Analysis (1920x1080) | 18.4ms | ✅ |
| PSNR (1920x1080) | 18.2ms | ✅ |
| SSIM (1920x1080) | 95.2ms | ✅ |
| Noise estimate | 9.0ms | ✅ |
| Difference map | 21.9ms | ✅ |
