# MViewer Cache Specification

Runtime cache limits are byte budgets. The Pixel Inspector's Raw16 store is
independently LRU-evicted by the exact allocated `uint16_t` buffer capacity, rejects a
single entry larger than its budget, and is included in
`CacheManager::memoryUsageBytes()` and `MemoryTracker` samples. The disk
cache likewise enforces its configured limit from SQLite blob byte lengths;
it is not a soft cap.

## Overview

The cache system is a critical performance component. It provides multiple layers of caching to minimize redundant I/O, decoding, and thumbnail regeneration. The cache hierarchy is designed to provide predictable memory usage with maximum hit rates.

---

## Cache Hierarchy

```
CacheManager::instance()   (src/core/cache/CacheManager.h)
  levels: Metadata · Thumbnail · Preview · FullImage · Disk
  owns routing, cross-tier fallback, budgets, per-level stats, prefetch
      │
      ├─ ImageCache ───── RAM pixel pools, bounded LRU, one mutex per pool
      │                   Metadata 16 MB · Thumbnail 64 MB · Preview 256 MB · Viewer 512 MB
      ├─ CacheManager ─── object stores: ImageMetadata (50000 entries) + Raw16 (256 MB)
      ├─ DiskCache ────── SQLite blob store, key = file identity
      │                   100000 entries · 1 GB byte cap
      └─ ThumbnailCache ─ on-disk PNG thumbnails
                          key = path + mtime + size + requested size + schema 4
                          approximate LRU · 512 MiB budget · no index file
      │
      ▼
Source: Filesystem
```

| Tier | Implementation | Backing store | Budget |
| ------ | ---------------- | --------------- | -------- |
| Metadata / Thumbnail / Preview / FullImage | `CacheManager` → `ImageCache` pools | RAM | 16 MB / 64 MB / 256 MB / 512 MB |
| Raw16 inspector samples | `CacheManager` object store | RAM | 256 MB (allocated capacity) |
| Disk | `CacheManager` → `DiskCache` | SQLite blobs | 1 GB, 100000 entries |
| Thumbnails on disk | `ThumbnailCache` | PNG files | 512 MiB |

There is no GPU texture cache in this stack: GPU uploads belong to the viewer
(Qt/OpenGL), not to `CacheManager`.

---

## Tier Owner: CacheManager

### Purpose

`CacheManager` (`src/core/cache/CacheManager.h`) owns the tiers. It stores no
pixels itself: it routes a `CacheLevel` (`Metadata`, `Thumbnail`, `Preview`,
`FullImage`, `Disk`) to the right backend, falls back from memory to disk (and
back-fills the memory pool on a disk hit), and keeps per-level statistics.

### Characteristics

| Property | Value |
| ---------- | ------- |
| Levels | `Metadata`, `Thumbnail`, `Preview`, `FullImage`, `Disk` |
| Routing | Memory levels map onto the `ImageCache` pools; `FullImage` maps to `ImageCache::Viewer` |
| Fallback | `get(level, key, out)`: memory → disk; a disk hit is written back into the memory level |
| Object stores | `ImageMetadata` (50000 entries) and Raw16 buffers live in `CacheManager`, not in `ImageCache` |
| Statistics | `levelStats(level)` → bytes / entries / hits / misses |
| Prefetch | `prefetch(keys, level = FullImage)` warms keys that already exist on disk into one memory level |
| Thread safety | Own mutexes for the object stores; the pixel pools are locked by `ImageCache` |

### Operations

```cpp
class CacheManager {
public:
    static CacheManager& instance();

    void configure(const CacheConfig& cfg);     // applied once, at construction
    const CacheConfig& config() const;

    // Memory tier (bounded LRU is enforced by ImageCache).
    void putMemory(CacheLevel level, const std::string& key, const ImageData& img);
    bool getMemory(CacheLevel level, const std::string& key, ImageData& out);

    // Disk tier (SQLite, see DiskCache).
    void putDisk(const std::string& key, const ImageData& img);
    bool getDisk(const std::string& key, ImageData& out);

    // Combined: memory first, then disk with a memory back-fill.
    bool get(CacheLevel level, const std::string& key, ImageData& out);
    void put(CacheLevel level, const std::string& key, const ImageData& img);

    // Object stores.
    void putMetadata(const std::string& key, const mviewer::domain::ImageMetadata& meta);
    bool getMetadata(const std::string& key, mviewer::domain::ImageMetadata& out) const;
    bool hasMetadata(const std::string& key) const;
    void putRaw16(const std::string& key, std::shared_ptr<std::vector<uint16_t>> buf, int channels,
                  uint16_t maxSample);
    bool getRaw16(const std::string& key, std::shared_ptr<std::vector<uint16_t>>& out,
                  int& channels, uint16_t& maxSample) const;

    // Management.
    void erase(const std::string& key);        // drop the key from every level
    void invalidate(const std::string& key);   // same, including metadata + Raw16
    void clear();
    void clearMemory();
    void clearDisk();
    CacheLevelStats levelStats(CacheLevel level) const;
    size_t memoryUsageBytes() const;           // pixel pools + Raw16
    size_t raw16UsageBytes() const;
    size_t diskUsageBytes() const;

    // Prefetch / warming.
    void prefetch(std::function<std::vector<std::string>()> nextKeys,
                  CacheLevel level = CacheLevel::FullImage);
    void prefetch(const std::vector<std::string>& keys, CacheLevel level = CacheLevel::FullImage);
};
```

---

## Memory Tier: ImageCache

### Purpose

Store decoded pixel payloads in RAM. Avoid redundant file I/O and decode
operations. `ImageCache` (`src/core/image/ImageCache.h`) is the memory part of
the hierarchy; `CacheManager` is the only intended caller.

### Characteristics

| Property | Value |
| ---------- | ------- |
| Storage | System RAM (`ImageData` values) |
| Pools | `Metadata`, `Thumbnail`, `Preview`, `Viewer` (`LevelCount == 4`) |
| Default sizes | Metadata 16 MB, Thumbnail 64 MB, Preview 256 MB, Viewer 512 MB |
| Accounting | Real payload bytes per pool (`usedBytes`, `entryCount`, `totalUsedBytes`) |
| Eviction | LRU per pool: oldest keys are dropped until the incoming entry fits |
| Thread safety | One `std::mutex` per pool — never a global lock |

### Operations

```cpp
class ImageCache {
public:
    enum Level { Metadata, Thumbnail, Preview, Viewer, LevelCount };

    static ImageCache& instance();

    void put(Level level, const std::string& key, const ImageData& img);
    bool get(Level level, const std::string& key, ImageData& out);
    void remove(Level level, const std::string& key);
    void clear();

    void setCapacity(Level level, size_t maxBytes);  // applied by CacheManager::configure()
    size_t usedBytes(Level level) const;
    size_t entryCount(Level level) const;
    size_t totalUsedBytes() const;
};
```

### Cache Key

Keys are plain `std::string`s, not `CacheKey` structs. The repository builds them
with `ImageRepository::makeKey()` (`MetadataReader::key(path)` =
`path|fileSize|mtimeMsec`) and `ImageRepository::makeFrameKey(path, frameIndex,
decodeVariant)` for one frame of a sequence. File identity is therefore part of
the key: a changed mtime or size produces a different key.

### Eviction Policy

1. On insert, if `curBytes + incoming > maxBytes`, evict the oldest entries of that pool
2. An entry whose payload alone exceeds the pool budget is **refused** rather than inserted (the pool never stays over its cap)
3. `setCapacity()` trims immediately when the budget shrinks
4. `CacheManager::levelStats()` reports hits/misses per `CacheLevel`, not per pool

### Thread Safety

- One `std::mutex` per pool, held only for map/list operations
- Reader/writer contention is limited to the pool being touched, so thumbnail and viewer traffic do not serialize against each other
- Values are copied in and out (`ImageData`), so a returned buffer cannot be mutated by a later eviction

---

## Disk Tier: DiskCache

### Purpose

Persist decoded pixels across restarts so re-opening a folder does not pay the
decode cost again. `DiskCache` (`src/core/image/DiskCache.h`) stores blobs in
SQLite. (The RAM thumbnail pool is `ImageCache::Thumbnail`, documented above.)

### Characteristics

| Property | Value |
| ---------- | ------- |
| Backing | One SQLite database file, one connection per worker thread |
| Key | File identity (`path\|fileSize\|mtimeMsec`, see `ImageRepository::makeKey()`) |
| Limits | `maxEntries()` 100000 and `maxBytes()` 1 GB, enforced lazily on `put()` by dropping the oldest entries |
| Accounting | `totalBytes()` sums exact blob payload bytes, not page/allocated size |
| Enable switch | `setEnabled()` / `isEnabled()`, enabled by default |
| Pruning | `prune(validKeys)` drops entries whose source path is no longer present |
| Thread safety | One `std::recursive_mutex` serializes every DB-touching call; the shared `QSqlDatabase` is never used concurrently |

### Operations

```cpp
class DiskCache {
public:
    static DiskCache& instance();

    bool get(const std::string& key, ImageData& out);   // false on miss
    void put(const std::string& key, const ImageData& img);
    void remove(const std::string& key);                // used by repository invalidate

    size_t entryCount() const;
    size_t totalBytes() const;                          // exact payload bytes
    void setMaxEntries(int n);
    int maxEntries() const;
    void setMaxBytes(size_t n);
    size_t maxBytes() const;

    void clear();
    void prune(const std::set<std::string>& validKeys);
    void setEnabled(bool on);
    bool isEnabled() const;
};
```

Qt SQL types stay out of the header: the database lives behind the private
`DiskCache::Impl` PIMPL and the template connection is only reachable through
`connectionForThread()`.

---

## Thumbnail Tier: ThumbnailCache

### Purpose

Persist display-ready thumbnails across application restarts so a previously seen
image never has to be decoded again just to show its thumbnail.
`ThumbnailCache` (`src/thumbnailcache.h`) is a bounded, approximately-LRU folder
of PNG files; it is separate from the in-RAM thumbnail pool in `ImageCache`.

### Storage Location

| Platform | Path |
|----------|------|
| Windows | `<QStandardPaths::CacheLocation>\thumbnails\` (i.e. under `%LOCALAPPDATA%`), resolved through `mviewer::runtime::writableDirectory()` |
| Linux | `<QStandardPaths::CacheLocation>/thumbnails/` |

If the standard cache directory is not writable, `writableDirectory()` falls back
to `%TEMP%/MViewer/runtime/…`; if neither works, `cacheDir()` returns empty and
the thumbnail tier is effectively disabled.

### File Layout

There is no index file and no sharding. The folder is flat, one `<key>.png` per
cached thumbnail, and the usage index is rebuilt in memory from the actual files
(`ensureIndexed()` / the bootstrap scan).

### File Naming

`ThumbnailCache::keyFor(path, size)` builds the file name from the source
identity plus the request:

```
<sha1(source path)>_<source mtime in ms>_<source size>_<requested size>_v<schema>
```

Consequences: a 64px thumbnail can never be served to a 240px request, and any
change in the source identity or in the payload schema (currently
`kSchemaVersion = 4` — the display-ready, ICC-converted payload) misses the old
files instead of reusing them.

### Operations

```cpp
class ThumbnailCache {
public:
    // Payload format / key semantics version (M36: display-ready, ICC-converted).
    static constexpr int kSchemaVersion = 4;
    static constexpr quint64 kDefaultMaxBytes = 512ULL * 1024ULL * 1024ULL;

    static ThumbnailCache& instance();
    static QString keyFor(const QString& path, int size);   // public for tests

    bool get(const QString& path, int size, QImage& out);      // hit refreshes recency
    void put(const QString& path, int size, const QImage& img); // atomic write, then prune
    void invalidatePath(const QString& path);                  // every revision of one source

    quint64 maxBytes() const;
    quint64 totalBytes();          // joins the bootstrap scan; actual on-disk bytes
    void setMaxBytes(quint64 n);   // shrinks the budget and prunes immediately
    void clear();
};
```

### Eviction Policy

1. `put()` evicts the least-recently-used entries known at the time while usage exceeds `maxBytes()`
2. Recency is a monotonic in-memory clock; startup seeds the order from file mtime, and a successful `get()` refreshes it
3. A file the filesystem refuses to delete keeps its bytes accounted and pruning moves on instead of looping (the cap is best-effort)
4. The first `get()`/`put()` starts a detached bootstrap scan; a cache hit never waits for it, while `totalBytes()`, `setMaxBytes()` and `clear()` join it for a complete accounting view

### Validation

- A corrupt entry is removed best-effort during `get()`
- Source identity is part of the key, so a changed mtime or size simply misses
- `invalidatePath()` removes every historical revision of one source (M56: used when a watcher reports an overwrite whose size and timestamp are unchanged)
- Files another process adds after the index was built are picked up and counted by `get()`

### Size Limits

| Property | Default | Range |
| ---------- | --------- | ------- |
| Thumbnail folder budget | 512 MiB (`kDefaultMaxBytes`) | any value `setMaxBytes()` accepts; pruning is immediate |
| Payload format | PNG (`QImage`), written atomically | temp file + rename, so a partial file is never cached |

---

## Cache Invalidation

### Triggers

| Event | Action |
| ------- | -------- |
| File modification time changed | The identity key changes; `ImageRepository::invalidate()` drops the old revision from every tier |
| File size changed | Same, both fields are part of the key |
| File deleted | `ImageRepository::release()` / `invalidate()` → `CacheManager::invalidate()`, plus `ThumbnailCache::invalidatePath()` for the thumbnails |
| File overwritten with unchanged size/mtime (watcher hint) | `invalidate()` drops the previously remembered revision as well (M56) |
| Application exit | Nothing to persist: the disk tier and the thumbnail folder are live stores, not flushed indexes |
| Cache cleared | `CacheManager::clear()` empties the memory and disk tiers; `ThumbnailCache::clear()` empties the thumbnail folder |

### Change Detection

There is no `enableFileWatching` / `fileWatchDebounceMs` setting; watching is a
compile-time behaviour of two UI-side helpers:

- `DirectoryMonitor` (`src/directorymonitor.{h,cpp}`, M56) watches the active
  browse directory with `QFileSystemWatcher`, treats the notification as a hint
  only, debounces through a single-shot timer (`kDebounceMs = 75`), then
  reconciles with a bounded async snapshot and latest-wins delivery. It also
  retries briefly for stability and reports when the directory becomes
  unavailable.
- `DirectoryTree` uses `QFileSystemWatcher` as a simple refresh hint for expanded
  nodes.

### Manual Invalidation

- Refresh / rebuild of the current folder → new key per changed file, plus `invalidate()` for the files the watcher reports
- Cache clearing is API-level (`CacheManager::clear()` / `clearMemory()` / `clearDisk()`, `ThumbnailCache::clear()`); no UI "clear cache" action is wired in this build
- Settings change → `CacheManager::configure()` re-applies capacities; shrunk `ImageCache` pools trim immediately

---

## Cache Warming

### On Folder Open

1. Scan the directory (async)
2. For each visible thumbnail, `ThumbnailProvider::produce(path, size)`:
   - `ThumbnailCache::get(path, size, img)` hit → use the persisted PNG
   - Else `Decoder::decodeScaled(path, size, meta)` → display conversion → square-fit → `ThumbnailCache::put()` on the worker thread
3. Generate thumbnails for the visible range first
4. Then generate for adjacent ranges (scroll buffer)

### On Image Display

`ImageRepository::load(path)` walks the tiers in this order:

1. `loadMemoryHit()`: `CacheManager::getMemory(CacheLevel::FullImage, key, pixels)` plus `getMetadata(key)` — a memory hit also restores the Raw16 buffer and the histogram
2. `loadPixels()`: when `useDiskCache` is set, `DiskCache::get(key, pixels)`
3. Else `Decoder::decodeFull(path, meta)`, then the pixels are cached with `CacheManager::putMemory(CacheLevel::FullImage, key, pixels)` and persisted through `DiskCache`

---

## Statistics & Monitoring

### Metrics

| Metric | Description |
| -------- | ------------- |
| Hit ratio | hits / (hits + misses) per layer |
| Eviction count | Number of evictions per layer |
| Total size | Current memory/disk usage |
| Entry count | Number of cached entries |
| Average entry size | Total size / entry count |

### Reporting

- `CacheManager::levelStats(level)` returns a `CacheLevelStats` snapshot (bytes, entries, hits, misses); `DiskCache::entryCount()` / `totalBytes()` and `ThumbnailCache::totalBytes()` report the persistent stores
- `CacheManager::memoryUsageBytes()` includes the Raw16 object store, so `MemoryTracker` samples see it
- Counters are process-lifetime totals (they are not reset by `clear()`, which only drops payloads)

---

## Thread Safety Summary

| Cache | Lock | Notes |
| ------- | ------ | ------- |
| `ImageCache` pools | One `std::mutex` per pool | Taken for map/list operations only; pools do not serialize against each other |
| `CacheManager` object stores | `std::mutex` for metadata, `std::mutex` for Raw16 | Separate from the pixel pools |
| `DiskCache` | One `std::recursive_mutex` | Serializes all statements on the shared `QSqlDatabase`; per-thread connections avoid cross-thread use |
| `ThumbnailCache` | `QMutex` for the index + a `std::mutex` for the bootstrap/invalidation queues | Disk I/O runs on the caller's thread; only the one-time bootstrap scan is detached |

---

## Configuration

`CacheConfig` (`src/core/cache/CacheManager.h`, RFC-003). `CacheManager` applies it
with `configure()` — once, from its constructor — which sets the four `ImageCache`
pool capacities, the Raw16 budget, and both `DiskCache` limits.

```cpp
struct CacheConfig
{
    size_t metadataCacheSize  = 16 * 1024 * 1024;  // 16MB  → ImageCache::Metadata
    size_t thumbnailCacheSize = 64 * 1024 * 1024;  // 64MB  → ImageCache::Thumbnail
    size_t previewCacheSize   = 256 * 1024 * 1024;  // 256MB → ImageCache::Preview
    size_t viewerCacheSize    = 512 * 1024 * 1024;  // 512MB → ImageCache::Viewer (FullImage)
    size_t raw16CacheSize     = 256 * 1024 * 1024;  // 256MB → 16-bit inspector samples
    size_t diskCacheSize      = 1024 * 1024 * 1024; // 1GB, byte cap on blob payloads → DiskCache
    int maxDiskCacheEntries   = 100000;             // → DiskCache::setMaxEntries()
};
```

The on-disk thumbnail budget is not part of `CacheConfig`: `ThumbnailCache` owns
its own cap (`maxBytes()` / `setMaxBytes()`, default `kDefaultMaxBytes` = 512 MiB).
