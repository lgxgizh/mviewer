# MViewer Benchmark Regression Report

**Date:** 2026-07-24 19:22:12

**Baseline:** D:\mviewer\benchmark\perf_baseline.json

| Scenario | Metric | Value | Passed | Regression |
|----------|--------|-------|--------|------------|
| B0 | cold_start_to_thumbnail_ms | 6.1743 | PASS | -59.1% |
| B1 | qt_event_loop_probe_ms | 0.0131 | PASS | -56.3% |
| B2 | first_thumbnail_ms | 17.5702 | PASS | +67.3% |
| TRACE | pipeline_priority | 0 | PASS | — |
| B3 | decode_p50_ms_jpeg | 17.6372 | PASS | — |
| B4 | thumbnails_per_sec | 66.9154 | PASS | — |
| B5 | cache_hit_ratio | 0.157 | PASS | — |
| B6 | peak_cache_bytes | 5.36347e+08 | PASS | — |
| B7 | switch_warm_p50_ms | 0.2409 | PASS | — |
| B8 | switch_p50_ms | 0.2411 | PASS | +201.4% |
| B9 | baseline_return_ok | 1 | PASS | — |
| B10 | hundred_mp_viewport_ms | 710.223 | PASS | — |

### Regressions (>10%)

- B2: +67.3% (10.500 -> 17.570)
- B8: +201.4% (0.080 -> 0.241)
