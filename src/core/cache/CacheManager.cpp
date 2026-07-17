#include "core/cache/CacheManager.h"

CacheManager &CacheManager::instance()
{
    static CacheManager inst;
    return inst;
}

CacheManager::CacheManager()
{
    configure(m_config);
}

void CacheManager::configure(const CacheConfig &cfg)
{
    m_config = cfg;
    ImageCache::instance().setCapacity(ImageCache::Metadata, cfg.metadataCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Thumbnail, cfg.thumbnailCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Preview, cfg.previewCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Viewer, cfg.viewerCacheSize);
    DiskCache::instance().setMaxEntries(cfg.maxDiskCacheEntries);
}

ImageCache::Level CacheManager::toImageCacheLevel(CacheLevel level) const
{
    switch (level)
    {
    case CacheLevel::Metadata:
        return ImageCache::Metadata;
    case CacheLevel::Thumbnail:
        return ImageCache::Thumbnail;
    case CacheLevel::Preview:
        return ImageCache::Preview;
    case CacheLevel::FullImage:
        return ImageCache::Viewer;
    case CacheLevel::Disk:
        return ImageCache::Viewer; // 内存路径不会走到这里
    }
    return ImageCache::Viewer;
}

void CacheManager::putMemory(CacheLevel level, const std::string &key, const ImageData &img)
{
    if (level == CacheLevel::Disk)
        return;
    ImageCache::instance().put(toImageCacheLevel(level), key, img);
}

bool CacheManager::getMemory(CacheLevel level, const std::string &key, ImageData &out)
{
    if (level == CacheLevel::Disk)
        return false;
    return ImageCache::instance().get(toImageCacheLevel(level), key, out);
}

void CacheManager::putDisk(const std::string &key, const ImageData &img)
{
    DiskCache::instance().put(key, img);
}

bool CacheManager::getDisk(const std::string &key, ImageData &out)
{
    return DiskCache::instance().get(key, out);
}

bool CacheManager::get(CacheLevel level, const std::string &key, ImageData &out)
{
    if (level == CacheLevel::Disk)
    {
        if (getDisk(key, out))
        {
            recordHit(level);
            return true;
        }
        recordMiss(level);
        return false;
    }
    if (getMemory(level, key, out))
    {
        recordHit(level);
        return true;
    }
    // 回退到磁盘层
    if (getDisk(key, out))
    {
        putMemory(level, key, out);
        recordHit(level);
        return true;
    }
    recordMiss(level);
    return false;
}

void CacheManager::put(CacheLevel level, const std::string &key, const ImageData &img)
{
    if (level == CacheLevel::Disk)
        putDisk(key, img);
    else
        putMemory(level, key, img);
}

CacheLevelStats CacheManager::levelStats(CacheLevel level) const
{
    CacheLevelStats s;
    s.hits = m_hits[static_cast<int>(level)].load();
    s.misses = m_misses[static_cast<int>(level)].load();
    if (level == CacheLevel::Disk)
    {
        s.entries = DiskCache::instance().entryCount();
        s.bytes = diskUsageBytes();
    }
    else
    {
        ImageCache::Level icl = toImageCacheLevel(level);
        s.entries = ImageCache::instance().entryCount(icl);
        s.bytes = ImageCache::instance().usedBytes(icl);
    }
    return s;
}

void CacheManager::erase(const std::string &key)
{
    ImageCache::instance().remove(ImageCache::Metadata, key);
    ImageCache::instance().remove(ImageCache::Thumbnail, key);
    ImageCache::instance().remove(ImageCache::Preview, key);
    ImageCache::instance().remove(ImageCache::Viewer, key);
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_metaStore.erase(key);
        m_metaOrder.remove(key);
    }
    DiskCache::instance().remove(key);
}

void CacheManager::clear()
{
    clearMemory();
    clearDisk();
}

void CacheManager::clearMemory()
{
    ImageCache::instance().clear();
}

void CacheManager::clearDisk()
{
    DiskCache::instance().clear();
}

size_t CacheManager::memoryUsageBytes() const
{
    return ImageCache::instance().totalUsedBytes();
}

size_t CacheManager::diskUsageBytes() const
{
    return DiskCache::instance().totalBytes();
}

void CacheManager::putMetadata(const std::string &key, const mviewer::domain::ImageMetadata &meta)
{
    std::lock_guard<std::mutex> lock(m_metaMutex);
    auto it = m_metaStore.find(key);
    if (it != m_metaStore.end())
    {
        m_metaOrder.remove(key);
    }
    else if (m_metaStore.size() >= kMetaMaxEntries)
    {
        const std::string victim = m_metaOrder.back();
        m_metaOrder.pop_back();
        m_metaStore.erase(victim);
    }
    m_metaStore[key] = meta;
    m_metaOrder.push_front(key);
}

bool CacheManager::getMetadata(const std::string &key, mviewer::domain::ImageMetadata &out) const
{
    std::lock_guard<std::mutex> lock(m_metaMutex);
    auto it = m_metaStore.find(key);
    if (it == m_metaStore.end())
        return false;
    out = it->second;
    return true;
}

bool CacheManager::hasMetadata(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(m_metaMutex);
    return m_metaStore.find(key) != m_metaStore.end();
}

void CacheManager::invalidate(const std::string &key)
{
    ImageCache::instance().remove(ImageCache::Metadata, key);
    ImageCache::instance().remove(ImageCache::Thumbnail, key);
    ImageCache::instance().remove(ImageCache::Preview, key);
    ImageCache::instance().remove(ImageCache::Viewer, key);
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_metaStore.erase(key);
        m_metaOrder.remove(key);
    }
    DiskCache::instance().remove(key);
}

void CacheManager::prefetch(std::function<std::vector<std::string>()> nextKeys, CacheLevel level)
{
    if (!nextKeys)
        return;
    prefetch(nextKeys(), level);
}

void CacheManager::prefetch(const std::vector<std::string> &keys, CacheLevel level)
{
    if (level == CacheLevel::Disk)
        return; // 磁盘层无需预热
    for (const std::string &key : keys)
    {
        ImageData img;
        if (getDisk(key, img))
            putMemory(level, key, img);
    }
}
