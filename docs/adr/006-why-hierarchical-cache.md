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
