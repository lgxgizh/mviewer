# CacheManager Specification

## Module
CacheManager + CacheConfig + CacheLevelStats

## Purpose
CacheManager is the **sole owner** of all image cache state. It routes reads/writes across 5 levels (Metadata → Thumbnail → Preview → FullImage → Disk), handles cross-layer fallback, eviction budgeting, prefetch coordination, and per-level statistics. Memory pools delegate to `ImageCache`; disk pool delegates to `DiskCache`. All public interfaces expose only std types.

## API

```cpp
enum class CacheLevel : uint8_t { Metadata, Thumbnail, Preview, FullImage, Disk };

struct CacheConfig {
    size_t metadataCacheSize  = 16 * 1024 * 1024;     // 16 MB
    size_t thumbnailCacheSize = 64 * 1024 * 1024;     // 64 MB
    size_t previewCacheSize   = 256 * 1024 * 1024;    // 256 MB
    size_t viewerCacheSize    = 512 * 1024 * 1024;    // 512 MB
    size_t diskCacheSize      = 1024 * 1024 * 1024;   // 1 GB (soft cap)
    int    maxDiskCacheEntries = 100000;
};

struct CacheLevelStats {
    size_t   bytes = 0;
    size_t   entries = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
};

class CacheManager {
public:
    static CacheManager& instance();

    void configure(const CacheConfig& cfg);
    const CacheConfig& config() const;

    // Memory-level (LRU eviction handled internally)
    void putMemory(CacheLevel level, const std::string& key, const ImageData& img);
    bool getMemory(CacheLevel level, const std::string& key, ImageData& out);

    // Disk-level
    void putDisk(const std::string& key, const ImageData& img);
    bool getDisk(const std::string& key, ImageData& out);

    // Combined: memory first, fallback to disk (promotes on hit)
    bool get(CacheLevel level, const std::string& key, ImageData& out);
    void put(CacheLevel level, const std::string& key, const ImageData& img);

    // Remove key from ALL levels (memory pixel pool + metadata + disk)
    void erase(const std::string& key);
    void invalidate(const std::string& key);

    // Per-level statistics
    CacheLevelStats levelStats(CacheLevel level) const;

    // Metadata object cache (ImageMetadata, not pixels)
    void putMetadata(const std::string& key, const mviewer::domain::ImageMetadata& meta);
    bool getMetadata(const std::string& key, mviewer::domain::ImageMetadata& out) const;
    bool hasMetadata(const std::string& key) const;

    // Management
    void clear();
    void clearMemory();
    void clearDisk();
    size_t memoryUsageBytes() const;
    size_t diskUsageBytes() const;

    // Prefetch: warm given keys into specified memory level (FullImage default)
    void prefetch(std::function<std::vector<std::string>()> nextKeys,
                  CacheLevel level = CacheLevel::FullImage);
    void prefetch(const std::vector<std::string>& keys,
                  CacheLevel level = CacheLevel::FullImage);
};
```

## Input

| Parameter | Type | Constraints | Default |
|-----------|------|-------------|---------|
| `level` | `CacheLevel` | — | — |
| `key` | `string` | Non-empty path or URL | — |
| `img` | `const ImageData&` | Non-null for put | — |
| `cfg` | `const CacheConfig&` | All sizes >0 | see struct |
| `nextKeys` | `function<vector<string>()>` | Lazily evaluated | — |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `getMemory/getDisk/get` | `bool` | True = hit; out filled. False = miss |
| `putMemory/putDisk/put` | `void` | Stores entry; evict triggered if over budget |
| `erase/invalidate` | `void` | Purges all levels for key |
| `levelStats` | `CacheLevelStats` | Snapshot of current level |
| `memoryUsageBytes` | `size_t` | Sum of memory pools |
| `diskUsageBytes` | `size_t` | SQLite cache size |

## Ownership

- CacheManager **owns** all cache storage (ImageCache instances + DiskCache + metadata store).
- Callers receive `ImageData` by value (copy on get); no shared ownership.
- `CacheConfig` is owned by CacheManager (mutable via `configure`).

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `get/put/erase/invalidate` | Any thread | Per-pool mutex (ImageCache) + atomic hit/miss counters |
| `getMetadata/putMetadata` | Any thread | `m_metaMutex` |
| `prefetch` | Background only | Queues to TaskScheduler |
| `configure` | Main thread only | Not thread-safe; call once at startup |

## Memory

| Level | Capacity | Bound |
|-------|----------|-------|
| Metadata | 16 MB | 50k entries × ~320 bytes |
| Thumbnail | 64 MB | 256×256 RGB thumbnails |
| Preview | 256 MB | 1024×1024 preview images |
| FullImage | 512 MB | Full-resolution frames |
| Disk | 1 GB (soft) | SQLite blobs; `maxDiskCacheEntries` hard cap |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| Memory hit | <1 ms | ~0.01 ms |
| Disk hit | <10 ms | ~7 ms |
| Cold decode (24 MP JPEG) | <50 ms | ~25 ms |
| `prefetch(10)` | <300 ms total | Pipeline with Decode pool |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `null key` | Invalid input | No-op; return false |
| `disk full` | SQLite write fail | Memory cache only; log warning |
| `decode failed` | Corrupt cached blob | Remove entry; return false |
| `eviction pressure` | Over budget | Evict LRU until under capacity |

## Examples

```cpp
// Configure at startup
CacheConfig cfg;
cfg.viewerCacheSize = 1024 * 1024 * 1024; // 1 GB
CacheManager::instance().configure(cfg);

// Load into cache
ImageData img = decode("photo.jpg");
CacheManager::instance().put(CacheLevel::FullImage, "photo.jpg", img);

// Retrieve
ImageData out;
bool hit = CacheManager::instance().get(CacheLevel::FullImage, "photo.jpg", out);

// Prefetch upcoming images
CacheManager::instance().prefetch({"next1.jpg", "next2.jpg"});

// Invalidate after external change
CacheManager::instance().invalidate("photo.jpg");
```

## Unit Tests

```cpp
TEST(Cache, PutGetMemory) {
    CacheManager::instance().putMemory(CacheLevel::FullImage, "key", makeTestData(10,10));
    ImageData out;
    EXPECT_TRUE(CacheManager::instance().getMemory(CacheLevel::FullImage, "key", out));
    EXPECT_EQ(out.width, 10);
}

TEST(Cache, MissReturnsFalse) {
    ImageData out;
    EXPECT_FALSE(CacheManager::instance().getMemory(CacheLevel::FullImage, "nonexistent", out));
}

TEST(Cache, EraseClearsMetadata) {
    CacheManager::instance().putMetadata("f", makeMeta());
    CacheManager::instance().erase("f");
    ImageMetadata out;
    EXPECT_FALSE(CacheManager::instance().hasMetadata("f"));
}

TEST(Cache, LevelStatsTrackHits) {
    CacheManager::instance().clear();
    CacheManager::instance().putMemory(CacheLevel::Thumbnail, "a", makeTestData(10,10));
    ImageData out;
    CacheManager::instance().getMemory(CacheLevel::Thumbnail, "a", out);
    auto stats = CacheManager::instance().levelStats(CacheLevel::Thumbnail);
    EXPECT_GE(stats.hits, 1);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Cache::memoryUsage`.

## Future Extension

- Per-image priority scoring (keep high-priority images longer)
- Compressed memory cache (BC1/BC3 block compression)
- Distributed cache backend (Redis/memcached for multi-instance)
- Predictive prefetch (ML-based next-image prediction)
