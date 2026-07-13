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

    // 单例：ImageRepository 是图像生命周期的唯一管理者（RFC-005）。
    static ImageRepository& instance();

    // Synchronous load (uses DiskCache internally)
    Result load(const std::string& filePath, const LoadOptions& opts = kDefaultLoadOptions);

    // Async load with callback (dispatched via TaskScheduler)
    void loadAsync(const std::string& filePath,
                   std::function<void(const Result&)> callback,
                   const LoadOptions& opts = kDefaultLoadOptions);

    // Batch load directory
    std::vector<Result> loadDirectory(const std::string& dirPath, int maxImages = 1000);

    // 预取：在后台把图像解码并预热到缓存（不阻塞调用线程）。
    void prefetch(const std::string& filePath, const LoadOptions& opts = kDefaultLoadOptions);

    // 释放：丢弃该路径在所有缓存层中的条目（仓库释放其生命周期所有权）。
    void release(const std::string& filePath);

    // 轻量元数据：不解码像素，仅文件级信息（路径/尺寸/大小/mtime/哈希）。
    // 文件不存在时返回默认（空）ImageMetadata。
    mviewer::domain::ImageMetadata metadata(const std::string& filePath) const;

    // Save to disk cache explicitly
    void cacheToDisk(const std::string& filePath);

    // Invalidate cached entries (specific path or all)
    void invalidate(const std::string& filePath);
    void invalidateAll();

private:
    ImageRepository() = default;
    std::string makeKey(const std::string& filePath) const;
    mviewer::domain::ImageMetadata makeMeta(const std::string& filePath) const;
};
