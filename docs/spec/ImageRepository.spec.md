# ImageRepository Specification

## Module

ImageRepository

## Purpose

ImageRepository is the single entry point for managing image lifecycle: load, cache, decode, release. Other modules MUST NOT create ImageFrame directly.

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
| ----------- | ------ | ------------- | --------- |
| `filePath` | `std::string` | Valid UTF-8 path, non-empty | — |
| `opts.useDiskCache` | `bool` | — | `true` |
| `opts.generateHistogram` | `bool` | — | `true` |
| `opts.maxEdgeForThumbnail` | `int` | >0 | `256` |
| `callback` | `function<void(Result)>` | Non-null for async | — |
| `dirPath` | `std::string` | Valid directory | — |
| `maxImages` | `int` | >0, ≤10000 | `1000` |

## Output

| Method | Return | Semantics |
| -------- | -------- | ----------- |
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

- Repository owns the cache hierarchy (memory + disk).
- Caller receives `shared_ptr<ImageFrame>` (shared ownership of the domain object).
- Repository retains cache entries per CacheManager policy.
- ImageFrame is immutable after construction; cache/selection state is atomic.

## Thread Safety

| Method | Thread | Mechanism |
| -------- | -------- | ----------- |
| `load` | Any thread | CacheManager per-pool mutex |
| `loadAsync` | Any thread submit; UI thread callback | TaskScheduler LambdaTask + QueuedConnection |
| `loadDirectory` | Any thread | spawns N async tasks |
| `prefetch` | Background only | TaskScheduler Background queue |
| `release` | Any thread | CacheManager invalidate (mutex) |
| `metadata` | Any thread | CacheManager metadata mutex |

## Memory

| Path | Dominant | Bound |
| ------ | ---------- | ------- |
| `load` (cold) | ImageData pixels | Bounded by Viewer cache (512 MB LRU) |
| `load` (warm) | None (cache hit) | — |
| `loadDirectory` | N × ImageData | N ≤ maxImages; evicted by LRU under pressure |
| `metadata` | ImageMetadata | ≤ 50k entries (16 MB metadata pool) |

## Performance

| Scenario | Budget | Baseline |
| ---------- | -------- | ---------- |
| `loadAsync` dispatch | <1 ms | immediate callback on UI thread |
| Disk cache hit → ImageFrame | <5 ms | — |
| Cold decode (1920x1080 JPEG) | <50 ms | 24.7 ms |
| loadDirectory (1000 images) | <500 ms scan | — |
| `release` | <10 ms | erase + cancel in-flight |

## Errors

| Error | Cause | Recovery |
| ------- | ------- | ---------- |
| `success() == false` | Decode failed, corrupt file | Log; UI shows placeholder |
| `error.find("permission") == 0` | File locked by another process | Retry with backoff |
| IOException during load | Disk/network failure | Degrade to memory-only mode |

## Unit Tests

```cpp
TEST(Repository, LoadValidImage) {
    auto r = ImageRepository::instance().load("testdata/golden/256x256_gradient.png");
    EXPECT_TRUE(r.success());
    EXPECT_FALSE(r.frame->pixels().isNull());
}

TEST(Repository, DiskCacheHit) {
    auto r1 = ImageRepository::instance().load("testdata/golden/256x256_gradient.png");
    EXPECT_TRUE(r1.success());
    auto r2 = ImageRepository::instance().load("testdata/golden/256x256_gradient.png");
    EXPECT_TRUE(r2.fromCache);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenarios.

## Future Extension

- Streaming decode for very large images (>100 MP)
- Cloud/network storage backend (ImageRepository over HTTP)
- AI-powered auto-tagging on ingest
