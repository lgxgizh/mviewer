#pragma once
#include "core/image/ImageBuffer.h"
#include "core/image/ImageCache.h"
#include "core/image/DiskCache.h"
#include <string>
#include <vector>
#include <functional>

// Unified cache orchestration.
// Memory levels (Thumbnail / Preview / FullImage) are delegated to the
// existing ImageCache (LRU eviction per level); the Disk level is delegated
// to the existing DiskCache (SQLite). CacheManager owns no storage of its own,
// it only provides a single coherent API over the existing cache layers.
enum class CacheLevel : uint8_t { Thumbnail, Preview, FullImage, Disk };

class CacheManager
{
public:
    static CacheManager& instance();

    // Memory caches (LRU eviction handled by ImageCache)
    void putMemory(CacheLevel level, const std::string& key, const ImageData& img);
    bool getMemory(CacheLevel level, const std::string& key, ImageData& out);

    // Disk cache
    void putDisk(const std::string& key, const ImageData& img);
    bool getDisk(const std::string& key, ImageData& out);

    // Combined: memory first, then disk
    bool get(CacheLevel level, const std::string& key, ImageData& out);
    void put(CacheLevel level, const std::string& key, const ImageData& img);

    // Management
    void clear();
    void clearMemory();
    void clearDisk();
    size_t memoryUsageBytes() const;
    size_t diskUsageBytes() const;

    // Prefetch support (future: predictive cache)
    void prefetch(std::function<std::vector<std::string>()> nextKeys);

private:
    CacheManager() = default;
    static ImageCache::Level toImageCacheLevel(CacheLevel level);
};
