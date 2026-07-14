#pragma once
#include "core/image/DiskCache.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageCache.h"
#include "domain/Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 分层缓存的统一中枢（CacheManager 是「所有者」）。
// 内存级（Metadata / Thumbnail / Preview / FullImage）委托给既有 ImageCache
// （每级独立容量/淘汰/生命周期/互斥）；磁盘级委托给既有 DiskCache（SQLite）。
// CacheManager 不自己持有存储，而是负责：路由、跨级回退、淘汰预算、预取协调、
// 以及逐层统计。所有对外接口只暴露 std 类型。
enum class CacheLevel : uint8_t
{
    Metadata,
    Thumbnail,
    Preview,
    FullImage,
    Disk
};

// 分层容量配置（RFC-003）。
struct CacheConfig
{
    size_t metadataCacheSize = 16 * 1024 * 1024;  // 16MB
    size_t thumbnailCacheSize = 64 * 1024 * 1024; // 64MB
    size_t previewCacheSize = 256 * 1024 * 1024;  // 256MB
    size_t viewerCacheSize = 512 * 1024 * 1024;   // 512MB
    size_t diskCacheSize = 1024 * 1024 * 1024;    // 1GB（软上限，目前未严格计量）
    int maxDiskCacheEntries = 100000;
};

// 单级统计快照。
struct CacheLevelStats
{
    size_t bytes = 0;
    size_t entries = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
};

class CacheManager
{
public:
    static CacheManager& instance();

    // 应用容量配置（在构造后调用一次；默认配置见 CacheConfig）。
    void configure(const CacheConfig& cfg);
    const CacheConfig& config() const { return m_config; }

    // 内存级（LRU 淘汰由 ImageCache 负责）
    void putMemory(CacheLevel level, const std::string& key, const ImageData& img);
    bool getMemory(CacheLevel level, const std::string& key, ImageData& out);

    // 磁盘级
    void putDisk(const std::string& key, const ImageData& img);
    bool getDisk(const std::string& key, ImageData& out);

    // 组合：内存优先，未命中回退磁盘（命中后回填内存）
    bool get(CacheLevel level, const std::string& key, ImageData& out);
    void put(CacheLevel level, const std::string& key, const ImageData& img);

    // 从所有层级删除某 key（仓库释放/失效时使用）。
    void erase(const std::string& key);

    // 逐层统计
    CacheLevelStats levelStats(CacheLevel level) const;

    // 元数据对象缓存层（RFC-003 的 Metadata 级，存 ImageMetadata 而非像素）。
    void putMetadata(const std::string& key, const mviewer::domain::ImageMetadata& meta);
    bool getMetadata(const std::string& key, mviewer::domain::ImageMetadata& out) const;
    bool hasMetadata(const std::string& key) const;

    // 管理
    void clear();
    void clearMemory();
    void clearDisk();
    // 移除某 key 在全部层级（内存像素池 + 元数据对象 + 磁盘）的缓存。
    void invalidate(const std::string& key);
    size_t memoryUsageBytes() const;
    size_t diskUsageBytes() const;

    // 预取：把给定 key 预热到指定内存级（默认 FullImage）。
    void prefetch(std::function<std::vector<std::string>()> nextKeys,
        CacheLevel level = CacheLevel::FullImage);
    void prefetch(const std::vector<std::string>& keys, CacheLevel level = CacheLevel::FullImage);

private:
    CacheManager();
    ImageCache::Level toImageCacheLevel(CacheLevel level) const;
    void recordHit(CacheLevel level) { m_hits[static_cast<int>(level)].fetch_add(1); }
    void recordMiss(CacheLevel level) { m_misses[static_cast<int>(level)].fetch_add(1); }

    CacheConfig m_config;
    mutable std::atomic<uint64_t> m_hits[5] = {};
    mutable std::atomic<uint64_t> m_misses[5] = {};

    // Metadata 级的对象存储（ImageMetadata，独立于像素池）。
    mutable std::mutex m_metaMutex;
    std::unordered_map<std::string, mviewer::domain::ImageMetadata> m_metaStore;
    std::list<std::string> m_metaOrder;
    static constexpr size_t kMetaMaxEntries = 50000;
};
