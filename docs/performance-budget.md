# Performance Budget

## Budgets

| Operation | Budget | Measurement |
| ----------- | -------- | ------------- |
| Cold start | < 300ms | Process entry to first paint |
| Warm start (cached) | < 100ms | Process entry to first paint |
| Open folder (first thumbnail) | < 100ms | Folder open to first visible thumbnail |
| Switch image (preloaded) | < 16ms | Keypress to full render |
| Switch image (cold) | < 100ms | JPEG 24MP decode + upload |
| Thumbnail generation | background only | Never blocks UI |

## Implementation status (2026-07-20)

The browse decode path is **async** (off the UI thread):

- `ImageViewer::setImage` uses `ImageRepository::loadAsync` (DecodePool); the
  decoded `ImageFrame` is applied on the UI thread via `QMetaObject::invokeMethod`
  and surfaced to the analysis panel through the `imageReady(frame)` signal.
- `PreviewPanel::setImage` and the `MainWindow` browse flow (`itemClicked`,
  `onImageOpen`, `navigate`, `navigateHistory`) no longer do a synchronous
  `QImage(path)` decode on the UI thread — the old blocking path that violated
  the first-thumbnail / switch-image budget is removed.
- Neighbor preloading warms the `ImageRepository` LRU without touching the
  visible frame (no `m_frame` write from the worker thread).

Net effect: opening an image and stepping with ←/→ no longer freeze the UI
thread while a 24MP+ image decodes. The remaining UI-thread cost per switch is
the cheap `rebuild()`/`update()` (thumbnail-grid + preview) plus the histogram
viz — well under the 16ms/20ms budget for cached images. Enforcing the budget
as a hard CI gate is tracked separately (Phase 2 / M17).

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
| -------- | -------- | -------- |
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

## Enforcement

Every PR must:

1. Run `benchmark_scenario.exe`
2. Compare results against this budget
3. Document any deviation
4. Architectural justification required for any exceedance
