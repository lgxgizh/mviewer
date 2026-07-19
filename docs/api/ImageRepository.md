# API — ImageRepository

**Header**: `src/core/image/ImageRepository.h`
**Layer**: core (Qt-free header; delegates to `ImageCache`/`DiskCache`)

## Purpose
Single entry point for loading decoded image pixels. Returns `ImageData`
(`core/image/ImageBuffer.h`) — a Qt-free pixel buffer — by path, with optional
downscaled decode and a cancellation handle. Results are served from the cache
tier managed by `CacheManager`.

## Interface (contract)
```cpp
namespace mviewer::core {

struct LoadOptions {
    int maxEdge = 0;          // 0 = full resolution; >0 = decode scaled to fit
    CacheLevel level = CacheLevel::Viewer;
    bool allowDisk = true;    // fall back to / populate DiskCache
};

class ImageRepository {
public:
    static ImageRepository &instance();                 // Meyers singleton

    ImageData load(const std::string &path,
                   const LoadOptions &opts = kDefaultLoadOptions);
    // Async variant: hands work to TaskScheduler; result delivered via callback
    // on the caller's pool. Returns a TaskHandle usable with TaskScheduler::cancel().
    TaskHandle loadAsync(const std::string &path,
                         const LoadOptions &opts,
                         std::function<void(ImageData)> onDone);

    static inline const LoadOptions kDefaultLoadOptions{};
};

} // namespace mviewer::core
```

## Cache levels
`Metadata | Thumbnail | Preview | Viewer` (see `CacheManager`). `load()` checks
memory cache → disk cache → decode, and writes back to both on miss.

## Thread-safety
Singleton construction is C++11-thread-safe. Pixel decode and cache mutation are
delegated to `ImageCache` (`pool.mtx`) and `DiskCache` (`s_createMutex`);
`ImageRepository.cpp` itself holds no locks (see M12.5 §5.2).

## Cancellation
`loadAsync` returns a `TaskHandle`; pass to `TaskScheduler::cancel(handle)` to
abort an in-flight decode. Already-delivered callbacks are not revoked.

## Error contract
On decode failure returns an empty `ImageData` (`!isValid()`); does not throw
across the public API. Callers check `ImageData::isValid()`.

## Status
✅ Stable. No change planned for M12.
