# Memory Budget

## Limits

| Layer | Default | Configurable |
| ------- | --------- | -------------- |
| Metadata cache | 16 MB | Yes |
| Thumbnail cache | 64 MB | Yes |
| Preview cache | 256 MB | Yes |
| Viewer cache | 512 MB | Yes |
| Raw16 cache (16-bit inspector samples) | 256 MB | Yes |
| Disk cache | 1 GB | Yes |
| Total memory budget | 1104 MB (sum of the five memory layers) | Yes (`CacheConfig`) |

## Per-Image Budget (24MP RGBA8)

| Representation | Size |
| ---------------- | ------ |
| Raw pixels | ~96 MB |
| Thumbnail (256x256) | ~256 KB |
| Preview (1024x768) | ~3 MB |
| QPixmap overhead | ~1.5x raw |

## Eviction

Each pool evicts LRU entries when exceeding budget.

- Order touched on every access.
- `ImageCache::evictIfNeeded` evicts victims one at a time, oldest-first, in a
  loop until the incoming bytes fit the pool budget (no batch/percentage
  eviction).

## Future Fine-Graining

- Pressure callback: system memory pressure triggers early eviction.
- Monitoring: `CacheManager::memoryUsageBytes()` exported for UI.
- `ImageFrame::ThumbnailData` can be a lower-resolution `ImageData` (future).
