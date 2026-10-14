# ADR 021 — Compare keeps only the on-screen pair in memory

## Context

Comparing two 4096×3072 JPGs grew the process to ~450 MB on Linux and ~850 MB
on Windows. Compare kept decoded frames for up to 12 entries / 384 MiB in its
session pool, prefetched the previous and next pairs at full resolution, and
every decode also stayed in the shared 512 MiB full-resolution RAM cache
(`ImageCache::Viewer`). Users pick compare pairs by hand in Browse, so the
in-Compare previous/next pair navigation that justified this retention is
removed.

## Decision

- `CompareSessionFramePool` retains only the current set: entries = pane count,
  bytes = loaded frame bytes (+1/16), clamped to [16 MiB, 384 MiB]. It is
  cleared before each new set is stored.
- No neighbour prefetch in Compare.
- `CacheManager::dropFullImage(key)` removes only the Viewer-level RAM entry.
  Compare calls it (via `ImageLoadingService::releaseFullPixels`) for paths
  that leave the screen and for every compared path when Compare closes. Disk
  cache, thumbnails, previews and metadata are untouched; frames still held by
  other consumers keep their shared pixels.
- `core::releaseFreeHeapToOs()` returns free heap to the OS after a load
  finishes and after Compare closes (Windows `_heapmin` +
  `SetProcessWorkingSetSize(-1, -1)`, glibc `malloc_trim(0)`).

## Consequences

- Linux, 6 × 4096×3072 JPG, open/compare/exit five times: in-Compare RSS
  ~453 → ~321 MB, after exit ~380 → ~189 MB, peak 511 → 353 MB.
- Re-opening a just-compared image decodes again (or hits the disk cache).
- Trimming the Windows working set makes the next touch of trimmed pages soft
  fault; it is done only at coarse points, never per frame.
