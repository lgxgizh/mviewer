#pragma once
#include "ImageFrame.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

// ImageRepository: abstraction over image lifecycle.
// Hides FileSystem + Decoder + Cache behind a single interface.
// Header is Qt-free; implementation may use Qt internally (.cpp).
class ImageRepository
{
public:
    struct LoadOptions {
        bool useDiskCache = true;
        bool generateHistogram = true;
        int maxEdgeForThumbnail = 256;
    };

    struct Result {
        std::shared_ptr<ImageFrame> frame;
        bool fromCache = false;
        std::string error;
        bool success() const { return frame != nullptr && error.empty(); }
    };

    static const LoadOptions kDefaultLoadOptions;

    // Synchronous load (uses DiskCache internally)
    Result load(const std::string& filePath, const LoadOptions& opts = kDefaultLoadOptions);

    // Async load with callback (dispatched via TaskScheduler)
    void loadAsync(const std::string& filePath,
                   std::function<void(const Result&)> callback,
                   const LoadOptions& opts = kDefaultLoadOptions);

    // Batch load directory
    std::vector<Result> loadDirectory(const std::string& dirPath, int maxImages = 1000);

    // Save to disk cache explicitly
    void cacheToDisk(const std::string& filePath);

    // Invalidate cached entries
    void invalidate(const std::string& filePath);
    void invalidateAll();

private:
    std::string makeKey(const std::string& filePath) const;
};
