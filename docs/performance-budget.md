# Performance Budget

## Budgets

| Operation | Budget | Measurement |
|-----------|--------|-------------|
| Cold start | < 300ms | Process entry to first paint |
| Warm start (cached) | < 100ms | Process entry to first paint |
| Open folder (first thumbnail) | < 100ms | Folder open to first visible thumbnail |
| Switch image (preloaded) | < 16ms | Keypress to full render |
| Switch image (cold) | < 100ms | JPEG 24MP decode + upload |
| Thumbnail generation | background only | Never blocks UI |
| UI response to mouse/keyboard | < 16ms | Input to frame render |
| Memory (normal workload) | < 500 MB | Private working set |
| Memory (large 24MP image decoded) | < 96 MB | raw pixel buffer |
| Histogram compute (24MP) | < 50 ms | Analysis thread |
| PSNR / SSIM / Noise compute | < 200 ms | Analysis thread |
| Difference map compute | < 100 ms | Analysis thread |
| Cache hit decode | < 1 ms | Memory cache → pixels |
| Cache promotion (disk → memory) | < 10 ms | SQLite blob → ImageData |

## Measurement

- Use `benchmark_scenario.exe` for automated benchmarks.
- Log p50 / p95 / p99 after each commit.
- Fail CI on regression > 10% vs baseline.

## Per-Module Tracked Metrics

| Module | Metric | Budget |
|--------|--------|--------|
| ImageRepository (cold load) | end-to-end load time | < 50 ms |
| ImageRepository (warm load) | shared_ptr + metadata return | < 5 ms |
| DiskCache (put) | SQLite insert + blob write | < 5 ms |
| DiskCache (get) | SQLite read + ImageData alloc | < 3 ms |
| TaskScheduler | task dispatch latency | < 0.5 ms |
| CompareSession::session() | copy of 8-image session | < 0.01 ms |
| RenderEngine::scale(1920→800) | Nearest / Bilinear / Bicubic | < 15 ms |
| AnalysisEngine::computeStats(24MP) | full histogram + stats | < 20 ms |
| AnalysisEngine::psnr(24MP) | pixel-compare | < 20 ms |

## Current Baselines (0.1.0)

| Operation | Avg | Status |
|-----------|-----|--------|
| Decode 1920x1080 (JPEG) | 24.7ms | ✅ |
| Encode 1920x1080 (JPEG) | 64.0ms | ✅ |
| ImageCache hit | <0.01ms | ✅ |
| CacheManager miss | 0.2ms | ✅ |
| Analysis (1920x1080) | 18.4ms | ✅ |
| PSNR (1920x1080) | 18.2ms | ✅ |
| SSIM (1920x1080) | 95.2ms | ✅ |
| Noise estimate | 9.0ms | ✅ |
| Difference map | 21.9ms | ✅ |

## Enforcement

Every PR must:
1. Run `benchmark_scenario.exe`
2. Compare results against this budget
3. Document any deviation
4. Architectural justification required for any exceedance
