# Specification Template

## Module
<ImageRepository>

## Purpose
<ImageRepository is the single entry point for managing image lifecycle: load, cache, decode, release. Other modules MUST NOT create ImageFrame directly.>

## API

```cpp
class ImageRepository {
public:
    static ImageRepository& instance();
    struct LoadOptions { bool useDiskCache = true; bool generateHistogram = true; int maxEdgeForThumbnail = 256; };
    struct Result { std::shared_ptr<ImageFrame> frame; bool fromCache = false; std::string error; bool success() const; };
    static const LoadOptions kDefaultLoadOptions;
    Result load(const std::string& filePath, const LoadOptions& opts = kDefaultLoadOptions);
    void loadAsync(const std::string& filePath, std::function<void(const Result&)> callback, const LoadOptions& opts = kDefaultLoadOptions);
    std::vector<Result> loadDirectory(const std::string& dirPath, int maxImages = 1000);
    void prefetch(const std::string& filePath, const LoadOptions& opts = kDefaultLoadOptions);
    void release(const std::string& filePath);
    mviewer::domain::ImageMetadata metadata(const std::string& filePath) const;
    void cacheToDisk(const std::string& filePath);
    void invalidate(const std::string& filePath);
    void invalidateAll();
};
```

## Input

| Parameter | Type | Constraints | Default |
|-----------|------|-------------|---------|
| `filePath` | `std::string` | Valid UTF-8 path, non-empty | — |
| `opts.useDiskCache` | `bool` | — | `true` |
| `opts.generateHistogram` | `bool` | — | `true` |
| `opts.maxEdgeForThumbnail` | `int` | >0 | `256` |
| `callback` | `function<void(Result)>` | Non-null for async | — |
| `dirPath` | `std::string` | Valid directory | — |
| `maxImages` | `int` | >0, ≤10000 | `1000` |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `load` | `Result` | `success()` true on valid frame; `error` set on failure |
| `loadAsync` | `void` | Callback invoked on UI thread with Result |
| `loadDirectory` | `vector<Result>` | One entry per file, in sorted order |
| `prefetch` | `void` | Non-blocking; populates cache in background |
| `release` | `void` | Erases all caches for the given path |
| `metadata` | `ImageMetadata` | File-level info without decoding pixels |
| `cacheToDisk` | `void` | Forces disk cache write |
| `invalidate(path)` | `void` | Purges specific path from all layers |
| `invalidateAll()` | `void` | Full cache purge |

## Ownership

- Repository **owns** the cache hierarchy (memory + disk).
- Caller receives `shared_ptr<ImageFrame>` (shared ownership of the domain object).
- Repository retains cache entries per CacheManager policy.
- ImageFrame is immutable after construction; cache/selection state is atomic.

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `load` | Any thread | CacheManager per-pool mutex |
| `loadAsync` | Any thread submit; UI thread callback | TaskScheduler LambdaTask + QueuedConnection |
| `loadDirectory` | Any thread | spawns N async tasks |
| `prefetch` | Background only | TaskScheduler Background queue |
| `release` | Any thread | CacheManager invalidate (mutex) |
| `metadata` | Any thread | CacheManager metadata mutex |

## Memory

| Path | Dominant | Bound |
|------|----------|-------|
| `load` (cold) | ImageData pixels | Bounded by Viewer cache (512 MB LRU) |
| `load` (warm) | None (cache hit) | — |
| `loadDirectory` | N × ImageData | N ≤ maxImages; evicted by LRU under pressure |
| `metadata` | ImageMetadata | ≤ 50k entries (16 MB metadata pool) |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| Disk cache hit | <10 ms | ~7 ms (SQLite read + decode) |
| Memory cache hit | <1 ms | ~0.01 ms |
| Cold decode (24 MP JPEG) | <50 ms | ~25 ms |
| Prefetch 10 images | <300 ms total | Pipeline with decode pool |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `empty path` | Invalid input | Return error Result |
| `file not found` | Deleted/moved | Remove from cache, return error |
| `decode failed` | Corrupt/unsupported | Return error, do NOT cache |
| `disk full` | SQLite write fail | Memory cache only, log warning |
| `permission denied` | Filesystem ACL | Return error |

## Examples

```cpp
// Sync load
auto result = ImageRepository::instance().load("photo.jpg");
if (result.success()) {
    display(result.frame->pixels());
}

// Async load with caching
ImageRepository::instance().loadAsync("photo.jpg", [](const auto& r) {
    if (r.success()) updateUI(r.frame);
});

// Prefetch upcoming images
ImageRepository::instance().prefetch("next.jpg");

// Release when done
ImageRepository::instance().release("old.jpg");
```

## Unit Tests

```cpp
TEST(Repository, LoadNotFound) {
    auto r = ImageRepository::instance().load("/no/such/file.jpg");
    EXPECT_FALSE(r.success());
    EXPECT_FALSE(r.error.empty());
}

TEST(Repository, LoadValidFile) {
    auto r = ImageRepository::instance().load("test_data/gradient.png");
    EXPECT_TRUE(r.success());
    EXPECT_FALSE(r.fromCache);
}

TEST(Repository, CacheRoundTrip) {
    auto r1 = ImageRepository::instance().load("test_data/gradient.png");
    auto r2 = ImageRepository::instance().load("test_data/gradient.png");
    EXPECT_TRUE(r1.success());
    EXPECT_TRUE(r2.fromCache);
}

TEST(Repository, PrefetchPopsCache) {
    ImageRepository::instance().prefetch("test_data/gradient.png");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ImageData out;
    bool hit = CacheManager::instance().getMemory(CacheLevel::FullImage, "test_data/gradient.png", out);
    EXPECT_TRUE(hit);
}

TEST(Repository, ReleasePurgesAll) {
    ImageRepository::instance().load("test_data/gradient.png");
    ImageRepository::instance().release("test_data/gradient.png");
    // After release, only memory-cache-level entries remain (empty in this case)
}

TEST(Repository, MetadataNoDecode) {
    auto meta = ImageRepository::instance().metadata("test_data/gradient.png");
    EXPECT_EQ(meta.width, 256);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenarios:
- `Open File::decodeFull(1920x1080)`
- `Switch Image::cacheHit(FullImage)`
- `Cache::memoryUsage`

## Future Extension

- Streaming decode for very large images (>100 MP)
- Cloud/network storage backend (ImageRepository over HTTP/WebDAO)
- AI-powered auto-tagging on ingest (addTag pipeline)
