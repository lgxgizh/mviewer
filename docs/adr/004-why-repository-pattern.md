# ADR-004: Why Repository Pattern for Image Lifecycle

## Status
Accepted

## Context
Images flow through multiple stages: filesystem → decode → cache → render. Without a central owner, each module tends to manage its own lifecycle, leading to leaks, double-decodes, and inconsistent cache state.

## Decision
**`ImageRepository`** becomes the single owner of image lifetime. Other modules request images from the repository; they never create `ImageFrame` directly.

## Rationale
- **Single ownership** — one object decides when to load, cache, and release
- **Lazy loading** — repository can defer decode until first access
- **Prefetching** — repository knows access patterns and can preload
- **Cache coherence** — all cache operations go through one path
- **Testability** — mock the repository to test consumers

## Responsibilities
- Directory scanning → list image paths
- Load image → return `ImageFrame` (from cache or decode)
- Save to disk cache
- Invalidate/release cached entries
- Prefetch management (future)

## Consequences
- ✅ No duplicate decode paths
- ✅ Centralized cache management
- ✅ Easy to add prediction/prefetch later
- ❌ Single point of contention (mitigated by thread-safe internals)

## Related
- RFC-005 (Repository lifecycle)
- RFC-003 (Cache pipeline)
