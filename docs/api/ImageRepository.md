# API — ImageRepository

**Header**: `src/core/image/ImageRepository.h`
**Layer**: core (Qt-free header; delegates to `ImageCache`/`DiskCache`)

## Purpose
Single entry point for loading decoded image pixels. `load()` returns a `Result`
value that owns a `shared_ptr<ImageFrame>` (an `ImageData` buffer plus
`mviewer::domain::ImageMetadata`) or carries an error string. Results are served
from the cache tiers managed by `CacheManager`.

## Interface (contract)
```cpp
// Global scope — ImageRepository and its options are not inside a namespace.
struct ImageLoadOptions {
    bool useDiskCache = true;       // consult / populate DiskCache
    bool generateHistogram = true;
    int maxEdgeForThumbnail = 256;  // longest edge for the thumbnail tier
    int frameMaxEdge = 0;           // M57 bounded frame prefetch; 0 = native frame
};

class ImageRepository {
public:
    class AsyncRequestState;                          // opaque, defined in the .cpp
    using AsyncRequestHandle = std::shared_ptr<AsyncRequestState>;
    using LoadOptions = ImageLoadOptions;   // the alias callers actually use

    struct Result {
        std::shared_ptr<ImageFrame> frame;
        bool fromCache = false;
        std::string error;
        bool success() const;               // frame != nullptr && error.empty()
    };

    static inline const LoadOptions kDefaultLoadOptions{};
    static ImageRepository &instance();     // Meyers singleton

    // Synchronous load (uses DiskCache internally).
    Result load(const std::string &path,
                const LoadOptions &opts = kDefaultLoadOptions);

    // Async variant: dispatched via TaskScheduler; the callback runs on a worker
    // thread and receives the Result. Returns nothing — cancellation goes through
    // the cancellable overload below.
    void loadAsync(const std::string &path,
                   std::function<void(const Result &)> callback,
                   const LoadOptions &opts = kDefaultLoadOptions);

    // Cancellable foreground load: returns an opaque handle for cancelAsync().
    AsyncRequestHandle loadAsyncCancellable(const std::string &path,
                                            std::function<void(const Result &)> callback,
                                            const LoadOptions &opts = kDefaultLoadOptions,
                                            std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime = {});
    void cancelAsync(AsyncRequestHandle &handle);
};
```

## Cache levels
`Metadata | Thumbnail | Preview | FullImage | Disk` (`CacheLevel`, see
`CacheManager`). There is no `Viewer` level: `FullImage` is the full-resolution
memory tier (`ImageCache::Viewer` is that pool's internal name). `load()` checks
the memory tier → `DiskCache` → decode, and writes back to both on a miss.

## Thread-safety
Singleton construction is C++11-thread-safe. Pixel decode and cache mutation are
delegated to `ImageCache` (one mutex per pool) and `DiskCache` (an internal
recursive mutex). `ImageRepository` itself keeps one lock: the path → identity-key
map (`m_keyByPath`) is guarded by `m_keyMtx`, because `makeKey()` / `rememberKey()`
are called from worker threads.

## Cancellation
`loadAsync` returns no handle. Use `loadAsyncCancellable()`, which returns an
`AsyncRequestHandle` for `cancelAsync(handle)`; the handle is nulled by the
cancel call. A cancelled request never fires its client callback, and a rejected
(non-cancelled) submission still reports exactly once with an explicit error.

## Error contract
A failed load returns a `Result` whose `error` is non-empty (for example
`decode failed: <path>`); `success()` is false and `frame` may be null. Nothing
throws across the public API.

## Status
✅ Stable. No change planned for M12.
