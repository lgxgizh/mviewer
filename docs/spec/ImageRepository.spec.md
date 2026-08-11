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
    AsyncRequestHandle loadAsyncCancellable(const std::string& filePath, std::function<void(const Result&)> callback, const LoadOptions& opts = kDefaultLoadOptions);
    AsyncRequestHandle preloadAsync(const std::string& filePath);
    AsyncRequestHandle promotePreloadAsync(AsyncRequestHandle& preload, std::function<void(const Result&)> callback);
    void cancelAsync(AsyncRequestHandle& handle);
    std::vector<Result> loadDirectory(const std::string& dirPath, int maxImages = 1000);
    void loadDirectoryAsync(const std::string& dirPath, std::function<void(std::vector<Result>)> callback, int maxImages = 1000);
    void prefetchVisible(const std::vector<std::string>& visiblePaths, const std::vector<std::string>& adjacentPaths = {});
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
| `loadAsync` | `void` | Callback invoked on the WORKER thread with Result; UI consumers marshal |
| `loadAsyncCancellable` | `AsyncRequestHandle` | Same semantics as `loadAsync`; the returned opaque handle lets the caller cancel obsolete work early |
| `preloadAsync` | `AsyncRequestHandle` | Best-effort cache warm at Background priority; no callback; may be cancelled |
| `promotePreloadAsync` | `AsyncRequestHandle` | Consumes a preload handle and promotes it to a foreground load delivered through the callback |
| `cancelAsync` | `void` | Flags the request cancelled, soft-cancels the scheduler task, clears the caller's handle |
| `loadDirectory` | `vector<Result>` | One entry per file, in sorted order; every submission accounted for |
| `loadDirectoryAsync` | `void` | Aggregate callback fires EXACTLY once with one Result per file |
| `prefetch` | `void` | Non-blocking; populates cache in background |
| `release` | `void` | Erases all caches for the given path |
| `metadata` | `ImageMetadata` | File-level info without decoding pixels |
| `cacheToDisk` | `void` | Forces disk cache write |
| `invalidate(path)` | `void` | Purges specific path from all layers |
| `invalidateAll()` | `void` | Full cache purge |

## Async completion contract (M26)

- `loadDirectoryAsync` never drops a submission silently: when the scheduler
  rejects a task (saturated / paused pool), that item becomes an explicit
  failure `Result` (`error` = "scheduler rejected submission for: ...") and
  still counts toward completion. The aggregate callback therefore fires
  exactly once, carrying one Result per file, in every scheduler state.
- `loadDirectory` (sync) does the same and never busy-waits forever: rejected
  items fail fast, and a defensive overall budget converts stragglers into
  timeout errors. It does NOT modify global scheduler queue-depth configuration
  (a caller's `setMaxQueueDepth` value survives the call).

## Preload promotion contract (M29)

`promotePreloadAsync(preload, callback)` CONSUMES the preload handle (a valid
Preload handle is consumed/reset) and converts the matching neighbor preload
into a foreground load whose result is delivered through `callback`:

- **Queued**: the stale Background task is cancelled and the load is
  resubmitted at Decode priority, so the foreground request is never starved
  behind other background work.
- **Running**: the in-flight decode is reused in place — no second decode is
  submitted — and `callback` is stashed to fire from the preload's terminal
  done path.
- **Finished / cancelled**: the load is resubmitted at Decode priority; a
  finished preload already warmed the FullImage memory cache, so the
  resubmitted `load()` resolves from cache.

The promoted `callback` runs on a WORKER thread (the scheduler terminal done
path), identical to `loadAsync`; UI consumers marshal themselves. A `nullptr`
handle (or a handle that is no longer a Preload) is an unchanged no-op — the
caller's handle is left as-is and the callback is never invoked. Cancellation
is best-effort: a running decode may finish and merely warm the cache. The
transient `Result` stashed on a preload is released at terminal completion — a
pure preload delivers nothing.

## Ownership

- Repository owns the cache hierarchy (memory + disk).
- Caller receives `shared_ptr<ImageFrame>` (shared ownership of the domain object).
- Repository retains cache entries per CacheManager policy.
- ImageFrame is immutable after construction; cache/selection state is atomic.

## Thread Safety

| Method | Thread | Mechanism |
| -------- | -------- | ----------- |
| `load` | Any thread | CacheManager per-pool mutex |
| `loadAsync` | Any thread submit; WORKER thread callback | TaskScheduler LambdaTask |
| `loadAsyncCancellable` | Any thread submit; WORKER thread callback | TaskScheduler LambdaTask; a cancelled request suppresses the client callback |
| `preloadAsync` | Any thread submit; Background worker | TaskScheduler Background queue; best-effort cache warm |
| `promotePreloadAsync` | Caller thread (exclusive access to the handle variable); WORKER thread callback | consumes the handle; Decode-priority resubmit or in-place reuse |
| `cancelAsync` | Any thread (exclusive access to the handle variable) | atomic cancelled flag + soft scheduler cancel; clears the caller's handle |
| `loadDirectory` | Any thread | spawns N async tasks; bounded wait |
| `loadDirectoryAsync` | Any thread; callback on worker or submitter | atomic completion counter |
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
