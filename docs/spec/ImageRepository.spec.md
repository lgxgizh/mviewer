# ImageRepository Specification

## Overview

ImageRepository is the single entry point for loading, caching, and managing image lifetime. Other modules must not create ImageFrame directly.

## API

```cpp
class ImageRepository {
public:
    struct LoadOptions {
        bool useDiskCache = true;
        bool generateHistogram = true;
        int maxEdgeForThumbnail = 256;
    };
    static const LoadOptions kDefaultLoadOptions;

    struct Result {
        std::shared_ptr<ImageFrame> frame;
        bool fromCache = false;
        std::string error;
        bool success() const { return frame != nullptr && error.empty(); }
    };

    // Synchronous load: disk cache → decoder → cache
    Result load(const std::string& path, const LoadOptions& opts = kDefaultLoadOptions);

    // Async load: dispatched via TaskScheduler::DecodePool
    void loadAsync(const std::string& path,
                   std::function<void(const Result&)> callback,
                   const LoadOptions& opts = kDefaultLoadOptions);

    // Batch: sequential async (directory scan)
    std::vector<Result> loadDirectory(const std::string& dir, int maxImages = 1000);

    // Management
    void cacheToDisk(const std::string& path);
    void invalidate(const std::string& path);
    void invalidateAll();
};
```

## Input

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `std::string` | Absolute file path |
| `opts` | `LoadOptions` | Behavior flags |
| `callback` | `function<void(Result)>` | Async completion handler |
| `dir` | `std::string` | Directory to scan |
| `maxImages` | `int` | Limit for batch load |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `load` | `Result` | `success()` or `error` |
| `loadAsync` | `void` | Callback invoked on UI thread |
| `loadDirectory` | `vector<Result>` | One per file |

## Ownership

- Repository **owns** the cache lifecycle
- Caller receives `shared_ptr<ImageFrame>` (shared ownership)
- Repository retains cache entry (weak_ptr or cache policy)

## Thread Safety

- `load`: safe from any thread (internal mutex on cache)
- `loadAsync`: always returns via UI-thread callback
- `loadDirectory`: spawns N async decodes

## Performance

| Path | Budget |
|------|--------|
| Disk cache hit | < 10ms |
| Memory cache hit | < 1ms |
| Cold decode | < 50ms (24MP JPEG) |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `empty path` | Invalid input | Return error Result |
| `file not found` | Deleted/moved | Remove from cache, return error |
| `decode failed` | Corrupt file | Return error, don't cache |
| `disk full` | SQLite write fail | Memory cache only, log warning |

## Examples

```cpp
// Sync load
auto result = ImageRepository::instance().load("photo.jpg");
if (result.success()) {
    display(result.frame->pixels());
}

// Async load
ImageRepository::instance().loadAsync("photo.jpg", [](const auto& r) {
    if (r.success()) updateUI(r.frame);
});
```

## Tests

```cpp
TEST(Repository, LoadNotFound) {
    auto r = ImageRepository::instance().load("/no/such/file.jpg");
    EXPECT_FALSE(r.success());
    EXPECT_FALSE(r.error.empty());
}

TEST(Repository, LoadCacheRoundTrip) {
    auto r1 = ImageRepository::instance().load("test.png");
    auto r2 = ImageRepository::instance().load("test.png");
    EXPECT_TRUE(r1.success());
    EXPECT_TRUE(r2.fromCache);
}
```
