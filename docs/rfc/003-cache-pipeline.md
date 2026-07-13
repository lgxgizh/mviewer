# RFC-003: Cache Pipeline

## Status
Accepted

## Priority
P0

## Goal
Introduce hierarchical cache with 5 levels. Each level has capacity, eviction, ownership, and lifetime.

## Current State
Simple ImageCache with 3 levels (Thumbnail/Preview/Viewer) + DiskCache. CacheManager is a thin wrapper.

## Target Architecture
```
Metadata Cache (in-memory, small)
    ↓
Thumbnail Cache (memory, LRU, ~64MB)
    ↓
Preview Cache (memory, LRU, ~256MB)
    ↓
Viewer Cache (memory, LRU, ~512MB)
    ↓
Disk Cache (SQLite, persistent, ~1GB)
```

## CacheManager
**Responsibilities:**
- Route get/put to correct layer
- Handle layer miss → fallback to next layer
- Eviction decisions (LRI/LRU per layer)
- Prefetch coordination
- Statistics (hit ratio, memory usage)

## Requirements

### Input
- CacheLevel enum (Metadata/Thumbnail/Preview/Viewer/Disk)
- Key (string: file identity hash)
- ImageData (for put)

### Output
- ImageData (for get, on hit)
- bool (for get, indicates hit/miss)

### Ownership
- CacheManager owns all cache layers
- Each layer owns its entries
- entries evicted when capacity exceeded

### Eviction
- LRU per layer
- Thumbnail: count-bounded
- Preview/Viewer: size-bounded
- Disk: size-bounded + persistent

### Thread Safety
- All operations thread-safe
- Per-layer mutex (not global)

### Performance
- Memory hit: <1ms
- Disk hit: <10ms
- Miss + decode: depends on decoder

### Error
- Layer failure → fallback to next layer
- All layers miss → return false (caller decodes)

## Configuration
```cpp
struct CacheConfig {
    size_t thumbnailCacheSize = 64 * 1024 * 1024;   // 64MB
    size_t previewCacheSize = 256 * 1024 * 1024;     // 256MB
    size_t viewerCacheSize = 512 * 1024 * 1024;      // 512MB
    size_t diskCacheSize = 1024 * 1024 * 1024;       // 1GB
    int maxDiskCacheEntries = 100000;
};
```

## Consequences
- Bounded, predictable memory
- Fast access to frequently-used images
- Cross-level consistency complexity

## Related
- RFC-002 (ImageFrame)
- ADR-006 (Why hierarchical cache)
