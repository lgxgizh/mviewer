#include "core/cache/CacheManager.h"

CacheManager& CacheManager::instance()
{
    static CacheManager inst;
    return inst;
}

ImageCache::Level CacheManager::toImageCacheLevel(CacheLevel level)
{
    switch (level) {
    case CacheLevel::Thumbnail: return ImageCache::Thumbnail;
    case CacheLevel::Preview:   return ImageCache::Preview;
    case CacheLevel::FullImage: return ImageCache::Viewer;
    case CacheLevel::Disk:      return ImageCache::Viewer; // never used for memory
    }
    return ImageCache::Viewer;
}

void CacheManager::putMemory(CacheLevel level, const std::string& key, const ImageData& img)
{
    if (level == CacheLevel::Disk) return;
    ImageCache::instance().put(toImageCacheLevel(level), key, img);
}

bool CacheManager::getMemory(CacheLevel level, const std::string& key, ImageData& out)
{
    if (level == CacheLevel::Disk) return false;
    return ImageCache::instance().get(toImageCacheLevel(level), key, out);
}

void CacheManager::putDisk(const std::string& key, const ImageData& img)
{
    DiskCache::instance().put(key, img);
}

bool CacheManager::getDisk(const std::string& key, ImageData& out)
{
    return DiskCache::instance().get(key, out);
}

bool CacheManager::get(CacheLevel level, const std::string& key, ImageData& out)
{
    if (level == CacheLevel::Disk)
        return getDisk(key, out);
    if (getMemory(level, key, out))
        return true;
    // Fall back to disk for memory levels
    if (getDisk(key, out)) {
        putMemory(level, key, out);
        return true;
    }
    return false;
}

void CacheManager::put(CacheLevel level, const std::string& key, const ImageData& img)
{
    if (level == CacheLevel::Disk)
        putDisk(key, img);
    else
        putMemory(level, key, img);
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
    // ImageCache owns the memory bookkeeping; expose a rough estimate by
    // summing decoded sizes is not currently tracked at this boundary.
    return 0;
}

size_t CacheManager::diskUsageBytes() const
{
    return 0;
}

void CacheManager::prefetch(std::function<std::vector<std::string>()> nextKeys)
{
    if (!nextKeys)
        return;
    for (const std::string& key : nextKeys()) {
        ImageData img;
        if (getDisk(key, img))
            putMemory(CacheLevel::FullImage, key, img);
    }
}
