# ADR-006: Why Hierarchical Cache (not single-level)

## Status

Accepted

## Context

A single cache can't balance speed vs. memory. Fast, small caches for thumbnails; large, slower caches for full-resolution images.

## Decision

Hierarchical cache with 5 levels: Metadata → Thumbnail → Preview → Viewer → Disk. Each level owns its capacity, eviction, and lifetime.

## Rationale

- **Fast path** — thumbnails served from memory, <16ms
- **Large storage** — disk cache survives restarts
- **Bounded memory** — each level has a budget
- **Layered fallback** — check L1 miss → L2 → L3 → L4 → decode

## Consequences

- ✅ Bounded, predictable memory
- ✅ Fast access to frequently-used images
- ❌ Cross-level consistency complexity
- ❌ Slightly higher code complexity

## Related

- RFC-003 (Cache pipeline)
- ADR-002 (ImageData)

## Amendment (2026-09-18) — O(1) LRU and oversized rejection

### Context

Viewer-level memory cache eviction previously scaled poorly under large entry
counts, and attempting to insert an entry larger than the level capacity could
flush already-resident working-set entries.

### Decision

- Keep the hierarchical levels from the original decision.
- Implement **O(1) LRU** unlink/relink for `ImageCache` / `CacheManager` eviction.
- **Reject oversized puts** (entry bytes > level capacity) without evicting
  existing entries; disk cache additionally bounds decoded width/height/pixels
  before allocation.

### Consequences

- ✅ Predictable latency under large warm caches
- ✅ Oversized frames cannot wipe a warm viewer cache
- ✅ Corrupt disk-cache rows cannot force huge allocations

## Amendment (2026-09-18) — Thumbnail Tier O(1) LRU & Oversized Rejection

### Context

Similar to the full-image cache, the Thumbnail tier (`ThumbnailPipeline` memory cache, `ThumbnailPanel` display pixmaps, and `ThumbnailCache` on-disk PNGs) previously allowed oversized payloads to trigger global eviction passes, wiping resident galleries. In addition, `ThumbnailPanel::enforceThumbPixmapBudgetLocked()` used an $O(N)$ linear scan per thumbnail delivery.

### Decision

- Implement **O(1) LRU** iterator tracking and splicing for `ThumbnailPanel::m_thumbReadyLru`.
- Add **oversized item rejection** across `ThumbnailPipeline::cacheLocked`, `ThumbnailPanel::ReadyPixmap`, and `ThumbnailCache::get`, ensuring candidates exceeding the budget are dropped before invoking eviction loops.
- Optimize `ThumbnailCache::ensureIndexed` with single-pass timestamp/size extraction to avoid $O(N \log N)$ filesystem metadata probes during directory bootstrap.

### Consequences

- ✅ Constant-time $O(1)$ thumbnail pixmap eviction during high-speed gallery scrolling.
- ✅ Complete immunity against cache blowout when handling pathological or corrupt thumbnails.
- ✅ Accelerated cold startup indexing over large persistent thumbnail caches.
