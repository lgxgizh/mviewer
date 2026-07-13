# Memory Budget

## Limits

| Layer | Default | Configurable |
|-------|---------|--------------|
| Thumbnail cache | 64 MB | Yes |
| Preview cache | 256 MB | Yes |
| Viewer cache | 512 MB | Yes |
| Disk cache | 1 GB | Yes |
| Total soft limit | 500 MB (runtime) | Hard limit |

## Per-Image Budget (24MP RGBA8)

| Representation | Size |
|----------------|------|
| Raw pixels | ~96 MB |
| Thumbnail (256x256) | ~256 KB |
| Preview (1024x768) | ~3 MB |
| QPixmap overhead | ~1.5x raw |

## Eviction

Each pool evicts LRU entries when exceeding budget.
- Order touched on every access.
- Eviction in batch (up to 10% of pool) to avoid thrashing.

## Future Fine-Graining

- Pressure callback: system memory pressure triggers early eviction.
- Monitoring: `CacheManager::memoryUsageBytes()` exported for UI.
- `ImageFrame::ThumbnailData` can be a lower-resolution `ImageData` (future).
