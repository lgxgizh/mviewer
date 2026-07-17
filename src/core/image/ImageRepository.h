#pragma once
#include "core/cache/CacheManager.h"

#include "ImageFrame.h"
#include "domain/Workspace.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ImageRepository: abstraction over image lifecycle.
// Hides FileSystem + Decoder + Cache behind a single interface.
// Header is Qt-free; implementation may use Qt internally (.cpp).
class ImageRepository
{
  public:
    struct LoadOptions
    {
        bool useDiskCache = true;
        bool generateHistogram = true;
        int maxEdgeForThumbnail = 256;
    };

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

  private:
    // Non-template implementation backing the public loadDirectoryAsync.
    // Invokes the callback directly (never checks operator bool — broken on
    // this toolchain).
    void loadDirectoryAsyncImpl(const std::string &dirPath,
                                std::function<void(std::vector<Result>)> callback, int maxImages);
};
