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
