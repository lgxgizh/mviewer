#pragma once
#include "core/image/DiskCache.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageCache.h"
#include "domain/Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Unified hub of the hierarchical cache (CacheManager is the "owner").
// Memory tiers (Metadata / Thumbnail / Preview / FullImage) are delegated to the existing ImageCache
// (Independent capacity/eviction/lifetime/mutex per level); the disk level is delegated to the existing DiskCache (SQLite).
// CacheManager does not hold storage itself, but is responsible for: routing, cross-tier fallback, eviction budget, prefetch coordination,
// and per-level statistics. All public interfaces expose only std types.
//
// True mipmap (in-memory): after a FullImage put, a power-of-two chain can be generated or lazily filled.
// Key scheme: baseKey + "#mip:" + lod (lod≥1); lod 0 is the FullImage itself.
// lod convention matches TileCache: 0 = finest, larger is coarser. mip pixels live in the Preview pool,
// Bounded by the Preview byte budget (shares eviction with other Preview entries).
enum class CacheLevel : uint8_t
{
    Metadata,
    Thumbnail,
    Preview,
    FullImage,
    Disk
};

// Hierarchical capacity configuration (RFC-003).
struct CacheConfig
{
    size_t metadataCacheSize = 16 * 1024 * 1024;  // 16MB
    size_t thumbnailCacheSize = 64 * 1024 * 1024; // 64MB
    size_t previewCacheSize = 256 * 1024 * 1024;  // 256MB
    size_t viewerCacheSize = 512 * 1024 * 1024;   // 512MB
    size_t raw16CacheSize = 256 * 1024 * 1024;    // 256MB (16-bit inspector samples)
    size_t diskCacheSize = 1024 * 1024 * 1024;    // 1GB（精确 byte 上限）
    int maxDiskCacheEntries = 100000;
};

// Single-level statistics snapshot.
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
    static CacheManager &instance();

    // Apply capacity configuration (call once after construction; see CacheConfig for the default configuration).
    void configure(const CacheConfig &cfg);
    const CacheConfig &config() const
    {
        return m_config;
    }

    // Memory level (LRU eviction is handled by ImageCache)
    void putMemory(CacheLevel level, const std::string &key, const ImageData &img);
    bool getMemory(CacheLevel level, const std::string &key, ImageData &out);

    // Disk tier
    void putDisk(const std::string &key, const ImageData &img);
    bool getDisk(const std::string &key, ImageData &out);

    // Composite: memory first, fall back to disk on miss (backfill memory after a hit)
    bool get(CacheLevel level, const std::string &key, ImageData &out);
    void put(CacheLevel level, const std::string &key, const ImageData &img);

    // Delete a key from all levels (used when the repository is released or invalidated).
    void erase(const std::string &key);
    // Drop only the in-memory full-resolution entry (Viewer level) for `key`.
    // Disk cache, thumbnails, previews and metadata are kept.
    void dropFullImage(const std::string &key);

    // Per-level statistics
    CacheLevelStats levelStats(CacheLevel level) const;

    // Metadata object cache layer (RFC-003 Metadata level; stores ImageMetadata rather than pixels).
    void putMetadata(const std::string &key, const mviewer::domain::ImageMetadata &meta);
    bool getMetadata(const std::string &key, mviewer::domain::ImageMetadata &out) const;
    bool hasMetadata(const std::string &key) const;

    // Raw 16-bit sample-buffer cache (Pixel Inspector high-bit-depth reads). Only when source bit depth > 8
    // Present, independent of the pixel pool; a separate cap on the vector's allocated capacity protects memory.
    void putRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> buf, int channels,
                  uint16_t maxSample);
    bool getRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> &out,
                  int &channels, uint16_t &maxSample) const;

    // ─── True in-memory mipmap ───────────────────────────────────────────────
    // Stable key for lod≥1 entries: baseKey + "#mip:" + lod.
    static std::string mipKey(const std::string &baseKey, int lod);

    // Store one mip level (lod≥1 → Preview pool; lod 0 → FullImage under baseKey).
    // A stored lod 0 drops any lod≥1 chain so a replaced full raster cannot
    // leave stale Preview mips. Replacing lod≥1 updates the chain byte total
    // by the difference, which trimMipsToBudget reads.
    void putMip(const std::string &baseKey, int lod, const ImageData &img);
    bool getMip(const std::string &baseKey, int lod, ImageData &out);

    // Largest stored level with max(w,h) ≤ maxEdge; if none, the next larger
    // (finest available that still exceeds maxEdge). Lazy-builds from FullImage
    // when baseKey has a FullImage hit but no mips yet.
    bool getBestMip(const std::string &baseKey, int maxEdge, ImageData &out);

    // Build + store lod≥1 from an already-known full raster (no FullImage put).
    // Idempotent when mips for baseKey already tracked. Returns level count
    // including lod 0 (1 = full only / too small to pyramid).
    int ensureMips(const std::string &baseKey, const ImageData &full, int minEdge = 256);

    // Drop lod≥1 Preview-pool mip entries for one base key. Thumbnail,
    // Metadata, FullImage (lod 0) and non-mip Preview entries are left alone.
    void dropMips(const std::string &baseKey);

    // Drop cold lod≥1 chains (keys absent from keepBaseKeys), largest first,
    // until tracked mip bytes are within maxBytes. Does not wipe other pools.
    size_t trimMipsToBudget(size_t maxBytes, const std::vector<std::string> &keepBaseKeys = {});

    // Management
    void clear();
    void clearMemory();
    void clearDisk();
    // Remove a key's cache from all tiers (memory pixel pool + metadata objects + disk + mips).
    void invalidate(const std::string &key);
    size_t memoryUsageBytes() const;
    size_t raw16UsageBytes() const;
    size_t raw16EntryCount() const;
    size_t diskUsageBytes() const;

    // Prefetch: warm the given key into the specified memory level (default FullImage).
    void prefetch(std::function<std::vector<std::string>()> nextKeys,
                  CacheLevel level = CacheLevel::FullImage);
    void prefetch(const std::vector<std::string> &keys, CacheLevel level = CacheLevel::FullImage);

  private:
    CacheManager();
    ImageCache::Level toImageCacheLevel(CacheLevel level) const;
    void recordHit(CacheLevel level)
    {
        m_hits[static_cast<int>(level)].fetch_add(1);
    }
    void recordMiss(CacheLevel level)
    {
        m_misses[static_cast<int>(level)].fetch_add(1);
    }

    void eraseMips(const std::string &baseKey);
    void storeMipChain(const std::string &baseKey, const std::vector<ImageData> &levels);
    bool getBestMipExisting(const std::string &baseKey, int maxEdge, ImageData &out);

    CacheConfig m_config;
    mutable std::atomic<uint64_t> m_hits[5] = {};
    mutable std::atomic<uint64_t> m_misses[5] = {};

    // Metadata-level object store (ImageMetadata, independent of the pixel pool).
    mutable std::mutex m_metaMutex;
    struct MetaEntry
    {
        mviewer::domain::ImageMetadata meta;
        std::list<std::string>::iterator orderIt;
    };
    std::unordered_map<std::string, MetaEntry> m_metaStore;
    mutable std::list<std::string> m_metaOrder;
    static constexpr size_t kMetaMaxEntries = 50000;

    // Raw 16-bit sample-buffer object store (independent of the pixel pool).
    mutable std::mutex m_raw16Mutex;
    struct Raw16Entry
    {
        std::shared_ptr<std::vector<uint16_t>> buf;
        int channels = 0;
        uint16_t maxSample = 0;
        std::list<std::string>::iterator orderIt;
    };
    std::unordered_map<std::string, Raw16Entry> m_raw16Store;
    mutable std::list<std::string> m_raw16Order;
    size_t m_raw16Bytes = 0;
    size_t m_raw16BudgetBytes = 256 * 1024 * 1024;

    // Tracks lod≥1 chains so erase/invalidate/trim can wipe them without
    // scanning ImageCache (no prefix API). `bytes` is lod≥1 payload only.
    struct MipChainInfo
    {
        int maxLod = 0;
        size_t bytes = 0;
    };
    mutable std::mutex m_mipMutex;
    std::unordered_map<std::string, MipChainInfo> m_mipChains;

    void eraseRaw16Locked(const std::string &key);
    void trimRaw16Locked();
};
