#pragma once
#include "core/cache/CacheManager.h"

#include "ImageFrame.h"
#include "domain/Workspace.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ImageRepository: abstraction over image lifecycle.
// Hides FileSystem + Decoder + Cache behind a single interface.
// Header is Qt-free; implementation may use Qt internally (.cpp).

// LoadOptions is a top-level (non-nested) struct. Keeping it independent of
// ImageRepository avoids a Clang clang-tidy co-compile diagnostic that fired
// when the nested struct's default member initializers were needed to
// initialize an inline static of the enclosing class outside a member
// function. Behavior is unchanged.
struct ImageLoadOptions
{
    bool useDiskCache = true;
    bool generateHistogram = true;
    int maxEdgeForThumbnail = 256;
};

class ImageRepository
{
  public:
    using LoadOptions = ImageLoadOptions;

    struct Result
    {
        std::shared_ptr<ImageFrame> frame;
        bool fromCache = false;
        std::string error;
        bool success() const
        {
            return frame != nullptr && error.empty();
        }
    };

    static inline const LoadOptions kDefaultLoadOptions{};

    static ImageRepository &instance();

    // Synchronous load (uses DiskCache internally)
    Result load(const std::string &filePath, const LoadOptions &opts = kDefaultLoadOptions);

    // Async load with callback (dispatched via TaskScheduler)
    void loadAsync(const std::string &filePath, std::function<void(const Result &)> callback,
                   const LoadOptions &opts = kDefaultLoadOptions);

    // M29 cancellable loads. AsyncRequestState is opaque (defined in the .cpp):
    // UI callers hold an AsyncRequestHandle and call cancelAsync() — they never
    // depend on TaskScheduler types. A cancelled request never fires its client
    // callback; a rejected (non-cancelled) submission still reports exactly once
    // with an explicit error (same contract as loadAsync).
    class AsyncRequestState;
    using AsyncRequestHandle = std::shared_ptr<AsyncRequestState>;

    // Cancellable foreground load: same semantics as loadAsync, but returns an
    // opaque handle that cancelAsync() can use to drop obsolete work early.
    AsyncRequestHandle loadAsyncCancellable(const std::string &filePath,
                                            std::function<void(const Result &)> callback,
                                            const LoadOptions &opts = kDefaultLoadOptions);

    // Low-priority neighbor preload (Background pool, disk cache allowed, no
    // histogram, no client callback). Best-effort: queued work may be skipped
    // by cancelAsync(); an already-running decode may finish and safely warm
    // the cache. Returns nullptr on scheduler rejection.
    AsyncRequestHandle preloadAsync(const std::string &filePath);

    // Consumes a preload handle: queued work is resubmitted at Decode priority,
    // running work is reused, finished work falls back to cache. nullptr is a
    // no-op.
    AsyncRequestHandle promotePreloadAsync(AsyncRequestHandle &preload,
                                           std::function<void(const Result &)> callback);

    // Mark the request cancelled (best-effort), soft-cancel the scheduler
    // handle, and clear the caller's handle. May be called from any thread, but
    // only when the caller has exclusive access to that handle variable (the
    // normal C++ shared_ptr object rules apply; it must not race another thread
    // that reads or writes the same shared_ptr). A null handle is a no-op.
    void cancelAsync(AsyncRequestHandle &handle);

    // Parallel directory load: dispatches each file to DecodePool using
    // TaskScheduler. This is synchronous (blocks until all done) but parallel
    // across all files.
    std::vector<Result> loadDirectory(const std::string &dirPath, int maxImages = 1000);

    // Async parallel directory load: dispatches files to DecodePool, calls
    // callback when all files are loaded (or errored).
    //
    // Takes std::function by value. NOTE: this toolchain's MSVC STL (19.51)
    // has a broken std::function operator bool (always returns false for
    // non-empty functions) but invocation works. The implementation therefore
    // NEVER checks operator bool on a std::function it intends to call — it
    // invokes directly and relies on the caller to pass a valid callable.
    void loadDirectoryAsync(const std::string &dirPath,
                            std::function<void(std::vector<Result>)> callback,
                            int maxImages = 1000);

    // Predictive preloading: prioritize visible images, prefetch neighbors.
    // visiblePaths = currently visible image paths (high priority).
    // adjacentPaths = next/prev N images around the visible set (background
    // priority).
    void prefetchVisible(const std::vector<std::string> &visiblePaths,
                         const std::vector<std::string> &adjacentPaths = {});

    // Prefetch given paths at specified cache level (default FullImage).
    void prefetch(const std::vector<std::string> &keys, CacheLevel level = CacheLevel::FullImage);

    // Release: drop this path from all cache layers.
    void release(const std::string &filePath);

    // Lightweight metadata: no pixel decode (path/size/mtime/hash).
    mviewer::domain::ImageMetadata metadata(const std::string &filePath) const;

    // Build a domain Workspace model from a root directory: recursively scan
    // for image files, group them by parent directory into Folders, each with
    // an ImageSet of lightweight ImageMetadata (no pixel decode). Used by the
    // UI to present the browsing model without loading any pixels.
    mviewer::domain::Workspace loadWorkspace(const std::string &rootPath, int maxPerFolder = 2000,
                                             bool recursive = true) const;

    // Save to disk cache explicitly.
    void cacheToDisk(const std::string &filePath);

    // Invalidate cached entries (specific path or all).
    void invalidate(const std::string &filePath);
    void invalidateAll();

    // Key derivation (shared with tests and advanced callers).
    std::string makeKey(const std::string &filePath) const;
    mviewer::domain::ImageMetadata makeMeta(const std::string &filePath) const;

    // M27: defensive budget for the synchronous loadDirectory(). Default is 5
    // minutes; tests shrink it to exercise the timeout path without waiting.
    // When the budget expires, outstanding accepted tasks are cancelled and
    // their slots become explicit "timed out" failure Results.
    void setSyncLoadBudget(std::chrono::milliseconds budget)
    {
        m_syncLoadBudgetMs.store(budget.count());
    }
    std::chrono::milliseconds syncLoadBudget() const
    {
        return std::chrono::milliseconds(m_syncLoadBudgetMs.load());
    }

  private:
    std::atomic<std::chrono::milliseconds::rep> m_syncLoadBudgetMs{5 * 60 * 1000};
    // Non-template implementation backing the public loadDirectoryAsync.
    // Invokes the callback directly (never checks operator bool — broken on
    // this toolchain).
    void loadDirectoryAsyncImpl(const std::string &dirPath,
                                std::function<void(std::vector<Result>)> callback, int maxImages);
};
