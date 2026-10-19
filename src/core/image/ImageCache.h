#pragma once

#include "core/image/DiskCache.h"

#include "ImageBuffer.h"

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

// Multi-level memory cache (the memory part of the tiered cache). Split into four tiers by purpose (all are
// LRU, each with its own independent capacity/eviction/lifecycle):
//  - Metadata: tiny images/file-level derived data (numerous, minimal footprint, byte-limited)
//  - Thumbnail: small images (list/gallery), many in count, small footprint
//  - Preview  : medium images (left-column preview), moderate quantity
//  - Viewer   : full-resolution decode results (double-click large image/compare), few in count, large footprint
// Each tier has its own mutex (not a global lock), scheduled and tracked centrally by CacheManager.
// Note: the interface exposes only std::string keys and ImageData values and does not depend on Qt types.
class ImageCache
{
  public:
    enum Level
    {
        Metadata,
        Thumbnail,
        Preview,
        Viewer,
        LevelCount
    };

    static ImageCache &instance();

    void put(Level level, const std::string &key, const ImageData &img);
    bool get(Level level, const std::string &key, ImageData &out);

    // Stored byte count; 0 if absent. Does not update the LRU.
    size_t storedBytes(Level level, const std::string &key) const;

    // Delete a single entry (used when the repository is released or invalidated).
    void remove(Level level, const std::string &key);

    void clear();

    // Set the capacity limit for a given level (bytes). CacheManager applies its CacheConfig after construction is complete.
    void setCapacity(Level level, size_t maxBytes);

    // Used bytes / entry count per tier (for CacheManager tiered statistics).
    size_t usedBytes(Level level) const;
    size_t entryCount(Level level) const;

    // Total bytes used by all memory pools
    size_t totalUsedBytes() const;

  private:
    ImageCache();

    struct Entry
    {
        ImageData img;
        size_t bytes = 0;
        std::list<std::string>::iterator orderIt;
    };

    struct Pool
    {
        size_t maxBytes = 0;
        size_t curBytes = 0;
        std::list<std::string> order; // 最近使用在前
        std::unordered_map<std::string, Entry> map;
        mutable std::mutex mtx;
    };

    void evictIfNeeded(Pool &pool, size_t incoming);
    void touch(Pool &pool, std::list<std::string>::iterator it);

    Pool m_pools[LevelCount];

    static constexpr size_t kMetaMaxBytes = 16 * 1024 * 1024;     // 16MB
    static constexpr size_t kThumbMaxBytes = 64 * 1024 * 1024;    // 64MB
    static constexpr size_t kPreviewMaxBytes = 256 * 1024 * 1024; // 256MB
    static constexpr size_t kViewerMaxBytes = 512 * 1024 * 1024;  // 512MB
};
