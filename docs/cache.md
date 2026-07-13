# MViewer Cache Specification

## Overview

The cache system is a critical performance component. It provides multiple layers of caching to minimize redundant I/O, decoding, and GPU operations. The cache hierarchy is designed to provide predictable memory usage with maximum hit rates.

---

## Cache Hierarchy

```
┌─────────────────────────────────────────────────────────┐
│                      User                                │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  L1: GPU Texture Cache                                  │
│  - VRAM-backed                                          │
│  - Fastest access (already on GPU)                      │
│  - Size: 512 MB (configurable)                          │
│  - Eviction: LRU                                        │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  L2: Decoded Image Cache                                │
│  - RAM-backed                                           │
│  - Avoids redundant decode                              │
│  - Size: 256 MB (configurable)                          │
│  - Eviction: LRU                                        │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  L3: Thumbnail Memory Cache                             │
│  - RAM-backed                                           │
│  - Avoids redundant thumbnail decode                    │
│  - Size: 64 MB (configurable)                           │
│  - Eviction: LRU                                        │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  L4: Thumbnail Disk Cache                               │
│  - Disk-backed (persistent)                             │
│  - Survives application restart                         │
│  - Size: 1 GB (configurable)                            │
│  - Eviction: LRU with size limit                        │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  Source: Filesystem                                     │
└─────────────────────────────────────────────────────────┘
```

---

## L1: GPU Texture Cache

### Purpose

Store textures already uploaded to GPU memory. Avoid redundant GPU uploads for recently viewed images.

### Characteristics

| Property | Value |
|----------|-------|
| Storage | VRAM (GPU memory) |
| Default size | 512 MB |
| Entry size | width × height × 4 bytes (RGBA8) |
| Max entries | ~128 (for 24MP images) |
| Eviction | LRU |
| Thread safety | Render thread only (GPU context) |

### Operations

```cpp
class GpuTextureCache {
public:
    /// Lookup texture by key
    std::optional<TextureHandle> find(const CacheKey& key);

    /// Insert texture, evict if necessary
    void insert(const CacheKey& key, TextureHandle texture, size_t size);

    /// Explicitly evict an entry
    void evict(const CacheKey& key);

    /// Evict entries until total size <= target
    void evictUntil(size_t targetSize);

    /// Clear all entries
    void clear();

    /// Current total size in bytes
    size_t totalSize() const;
};
```

### Eviction Policy

1. When inserting, check if `currentSize + entrySize > maxSize`
2. If yes, evict LRU entries until space is available
3. If single entry exceeds maxSize, reject insertion
4. Evicted textures must be destroyed (GPU resource freed)

### Memory Budget

- Default: 512 MB
- Minimum: 128 MB
- Maximum: 2 GB (or 25% of detected VRAM, whichever is smaller)
- User-configurable in settings

---

## L2: Decoded Image Cache

### Purpose

Store decoded pixel data in RAM. Avoid redundant file I/O and decode operations for recently viewed images.

### Characteristics

| Property | Value |
|----------|-------|
| Storage | System RAM |
| Default size | 256 MB |
| Entry size | width × height × bytesPerPixel |
| Max entries | ~6 (for 24MP RGBA8 images) |
| Eviction | LRU |
| Thread safety | `std::shared_mutex` (read-heavy) |

### Operations

```cpp
class DecodedImageCache {
public:
    /// Lookup decoded image by key
    std::shared_ptr<const DecodedImage> find(const CacheKey& key);

    /// Insert decoded image
    void insert(const CacheKey& key, std::shared_ptr<DecodedImage> image);

    /// Evict specific entry
    void evict(const CacheKey& key);

    /// Evict until total size <= target
    void evictUntil(size_t targetSize);

    /// Clear all entries
    void clear();

    /// Current total size in bytes
    size_t totalSize() const;

    /// Hit/miss statistics
    CacheStats stats() const;
};
```

### Cache Key

```cpp
struct CacheKey {
    std::filesystem::path filePath;
    int64_t fileSize;
    int64_t modificationTime;  // File mtime at time of decode
    DecodeParams params;       // Orientation, resolution limit
};
```

### Eviction Policy

1. On insert, if `totalSize + entrySize > maxSize`, evict LRU
2. Evicted entries trigger GPU texture eviction (if present)
3. Statistics tracked for hit ratio monitoring

### Thread Safety

- Reads: `std::shared_lock` (multiple concurrent readers)
- Writes: `std::unique_lock` (exclusive)
- Lock held for minimal time (just map operations)

---

## L3: Thumbnail Memory Cache

### Purpose

Store recently used thumbnails in RAM for instant display in the sidebar.

### Characteristics

| Property | Value |
|----------|-------|
| Storage | System RAM |
| Default size | 64 MB |
| Entry size | ~256 × 256 × 4 = ~256 KB |
| Max entries | ~256 |
| Eviction | LRU |
| Thread safety | `std::shared_mutex` |

### Thumbnail Specifications

| Property | Value |
|----------|-------|
| Default size | 256 × 256 pixels |
| Aspect ratio | Preserved (fit within box) |
| Format | RGBA8 |
| Quality | Lanczos3 resize |

### Operations

```cpp
class ThumbnailMemoryCache {
public:
    std::shared_ptr<const Thumbnail> find(const FilePath& path);
    void insert(const FilePath& path, std::shared_ptr<Thumbnail> thumbnail);
    void evict(const FilePath& path);
    void evictUntil(size_t targetSize);
    void clear();
    size_t totalSize() const;
};
```

---

## L4: Thumbnail Disk Cache

### Purpose

Persist thumbnails across application restarts. Avoid redundant thumbnail generation for previously seen images.

### Storage Location

| Platform | Path |
|----------|------|
| Windows | `%LOCALAPPDATA%\MViewer\thumbnails\` |
| Linux | `~/.cache/mviewer/thumbnails/` |

### Directory Structure

```
thumbnails/
├── index.json          // Cache index (path → metadata)
├── aa/                 // Sharded by hash prefix
│   ├── aabbccdd.png    // Thumbnail file
│   └── ...
├── ab/
│   └── ...
└── ...
```

### Cache Index Format

```json
{
    "version": 1,
    "entries": [
        {
            "path": "C:/Users/me/Photos/img001.jpg",
            "fileSize": 5242880,
            "modificationTime": 1700000000,
            "cacheFile": "aa/aabbccdd.png",
            "cachedAt": 1700000001,
            "width": 256,
            "height": 192
        }
    ]
}
```

### File Naming

- Hash of canonical file path → hex string
- First 2 characters = shard directory
- Remaining characters = filename
- Extension: `.png` (lossless, fast decode)

### Operations

```cpp
class ThumbnailDiskCache {
public:
    /// Initialize cache (load index, validate entries)
    std::expected<void, CacheError> initialize(const std::filesystem::path& cacheDir);

    /// Lookup thumbnail file path
    std::optional<FileInfo> find(const FilePath& path, int64_t fileSize, int64_t mtime);

    /// Write thumbnail to disk cache
    std::expected<FileInfo, CacheError> store(
        const FilePath& path, int64_t fileSize, int64_t mtime,
        const Thumbnail& thumbnail);

    /// Remove specific entry
    void remove(const FilePath& path);

    /// Evict entries until total size <= maxSize
    void evictUntil(size_t maxSize);

    /// Remove all entries
    void clear();

    /// Current total size on disk
    size_t totalSize() const;

    /// Validate all entries (remove stale/orphaned)
    void validate();
};
```

### Eviction Policy

1. On insert, if `totalSize + entrySize > maxSize`, evict LRU
2. LRU tracked via `cachedAt` timestamp in index
3. Orphaned files (no index entry) removed during validation
4. Stale entries (file changed) removed during lookup

### Validation

- Run on application startup
- Check each index entry against actual file
- Remove entries where file no longer exists or has changed
- Remove orphaned files with no index entry
- Rebuild index if corruption detected

### Size Limits

| Property | Default | Range |
|----------|---------|-------|
| Max disk cache size | 1 GB | 256 MB - 8 GB |
| Max entries | 100,000 | 10,000 - 500,000 |
| Max single thumbnail | 1 MB | — |

---

## Cache Invalidation

### Triggers

| Event | Action |
|-------|--------|
| File modification time changed | Invalidate L2, L3, L4 |
| File size changed | Invalidate L2, L3, L4 |
| File deleted | Invalidate all layers |
| File moved/renamed | Invalidate all layers |
| Application exit | Persist L4 index |
| User clears cache | Clear all layers |

### File Watching

- Use `ReadDirectoryChangesW` (Windows) or `inotify` (Linux)
- Watch current directory for changes
- Debounce rapid changes (500ms coalescing)
- On change notification, validate affected cache entries

### Manual Invalidation

- User action: "Refresh" → invalidate current folder
- User action: "Clear Cache" → clear all layers
- Settings change → adjust size limits, evict as needed

---

## Cache Warming

### On Folder Open

1. Scan directory (async)
2. For each visible thumbnail:
   - Check L3 (memory) → display if hit
   - Check L4 (disk) → decode and display, insert into L3
   - Else → enqueue thumbnail generation
3. Generate thumbnails for visible range first
4. Then generate for adjacent ranges (scroll buffer)

### On Image Display

1. Check L2 (decoded image cache) → upload to GPU if hit
2. Else → decode, insert into L2, upload to GPU
3. Preload neighbors (see preloading strategy)

---

## Statistics & Monitoring

### Metrics

| Metric | Description |
|--------|-------------|
| Hit ratio | hits / (hits + misses) per layer |
| Eviction count | Number of evictions per layer |
| Total size | Current memory/disk usage |
| Entry count | Number of cached entries |
| Average entry size | Total size / entry count |

### Reporting

- Statistics exposed via `CacheStats` struct
- Accessible from debug/performance overlay
- Logged on application shutdown
- Included in benchmark reports

---

## Thread Safety Summary

| Cache | Read Lock | Write Lock | Notes |
|-------|-----------|------------|-------|
| L1 GPU Texture | None (render thread only) | None | Single-threaded access |
| L2 Decoded Image | `std::shared_lock` | `std::unique_lock` | Read-heavy |
| L3 Thumbnail Memory | `std::shared_lock` | `std::unique_lock` | Read-heavy |
| L4 Thumbnail Disk | `std::shared_lock` | `std::unique_lock` | I/O bound |

---

## Configuration

```cpp
struct CacheConfig {
    // L1 GPU Texture Cache
    size_t gpuTextureCacheSize = 512 * 1024 * 1024;  // 512 MB

    // L2 Decoded Image Cache
    size_t decodedImageCacheSize = 256 * 1024 * 1024;  // 256 MB

    // L3 Thumbnail Memory Cache
    size_t thumbnailMemoryCacheSize = 64 * 1024 * 1024;  // 64 MB

    // L4 Thumbnail Disk Cache
    size_t thumbnailDiskCacheSize = 1024 * 1024 * 1024;  // 1 GB
    int maxDiskCacheEntries = 100000;

    // Behavior
    bool enableFileWatching = true;
    int fileWatchDebounceMs = 500;
};
```
