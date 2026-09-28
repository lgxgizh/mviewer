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

## Amendment (2026-09-27) — True in-memory mipmap chain

### Context

Compare already keeps an app-side display pyramid and a session frame pool, but
`CacheManager` only stored Metadata / Thumbnail / Preview / FullImage by path.
Consumers repeatedly re-scaled from full rasters for cheap LOD edges. TileCache
keys by `lod`, yet tiles were still built by scaling an already-decoded frame;
decoder-native disk-LOD remains a later milestone.

### Decision

- Add a **power-of-two in-memory mipmap chain** owned by `CacheManager`:
  - Helper: `MipmapPyramid::{buildMipChain,downscaleHalfBox}` (Qt-free box average).
  - LOD convention matches TileCache: **lod 0 = full / finest**; higher = coarser.
  - Key scheme: `baseKey + "#mip:" + lod` for lod≥1 (Preview pool budget); lod 0 is
    the FullImage entry under `baseKey`.
  - APIs: `putMip` / `getMip` / `getBestMip` / `ensureMips`.
  - FullImage `put`/`putMemory` eagerly builds lod≥1; `getBestMip` lazy-fills when
    only a too-large FullImage is present.
  - `erase` / `invalidate` / `clearMemory` wipe the whole chain for a base key.
- Wire Compare materialization to prefer `getBestMip` before `decodeLod` /
  `scaleBoundedStatic`.

### Consequences

- ✅ Cheap LOD / Compare first paint can reuse cached half/quarter rasters
- ✅ Mip bytes share the existing Preview budget (no unbounded growth)
- ✅ Invalidation stays coherent with path-keyed FullImage
- ❌ Decoder-native disk-LOD and TileCache paint rewrite still deferred
- ❌ Eager FullImage put pays a small CPU cost to build the chain
- ❌ B6 `peak_cache_bytes` hard gate raised to 896 MiB (Viewer+Preview)

### Related

- `docs/performance/COMPARE_LOAD_SMOOTHNESS.md` (Deferred → in-memory landed)
- ADR-006 original hierarchical levels

## Amendment (2026-09-28) — Targeted Preview mip release

### Context

Leaving a large viewer image, or closing Compare, kept lod≥1 Preview mips
resident for the rest of the session. `clearMemory` would also drop Thumbnail
and Metadata, which Browse still needs on the next folder.

### Decision

- Add `CacheManager::dropMips(baseKey)`: erase lod≥1 Preview entries for one
  base key. Lod 0 FullImage, Thumbnail, Metadata, and non-mip Preview keys stay.
- Add `CacheManager::trimMipsToBudget(maxBytes, keepBaseKeys)`: release cold
  lod≥1 chains, largest first, until tracked mip bytes fit the budget. Keys in
  `keepBaseKeys` stay. This is not a global `clearMemory`.
- Viewer replace/close and Compare session teardown call these for keys that
  are no longer on screen. UI reaches them through `core/image/DisplayMip.h`
  so product TUs do not include `CacheManager` directly.

### Consequences

- ✅ Leaving a large image frees Preview mip bytes without blanking the gallery
- ✅ The open image (or an explicit keep list) is not trimmed
- ✅ Thumbnail and Metadata caches survive the release
- ❌ FullImage lod 0 stays until ordinary FullImage eviction
