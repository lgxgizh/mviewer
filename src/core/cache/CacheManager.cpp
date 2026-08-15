#include "core/cache/CacheManager.h"

#include <limits>

CacheManager &CacheManager::instance()
{
    static CacheManager inst;
    return inst;
}

namespace
{
bool raw16ByteSize(const std::vector<uint16_t> &buf, size_t &bytes)
{
    if (buf.capacity() > (std::numeric_limits<size_t>::max() / sizeof(uint16_t)))
        return false;
    bytes = buf.capacity() * sizeof(uint16_t);
    return true;
}
} // namespace

CacheManager::CacheManager()
{
    configure(m_config);
}

void CacheManager::configure(const CacheConfig &cfg)
{
    m_config = cfg;
    {
        std::lock_guard<std::mutex> lock(m_raw16Mutex);
        m_raw16BudgetBytes = cfg.raw16CacheSize;
        trimRaw16Locked();
    }
    ImageCache::instance().setCapacity(ImageCache::Metadata, cfg.metadataCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Thumbnail, cfg.thumbnailCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Preview, cfg.previewCacheSize);
    ImageCache::instance().setCapacity(ImageCache::Viewer, cfg.viewerCacheSize);
    DiskCache::instance().setMaxEntries(cfg.maxDiskCacheEntries);
    DiskCache::instance().setMaxBytes(cfg.diskCacheSize);
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
    {
        std::lock_guard<std::mutex> lock(m_raw16Mutex);
        eraseRaw16Locked(key);
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
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    m_raw16Store.clear();
    m_raw16Order.clear();
    m_raw16Bytes = 0;
}

void CacheManager::clearDisk()
{
    DiskCache::instance().clear();
}

size_t CacheManager::memoryUsageBytes() const
{
    return ImageCache::instance().totalUsedBytes() + raw16UsageBytes();
}

size_t CacheManager::raw16UsageBytes() const
{
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    return m_raw16Bytes;
}

size_t CacheManager::raw16EntryCount() const
{
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    return m_raw16Store.size();
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

void CacheManager::putRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> buf,
                            int channels, uint16_t maxSample)
{
    if (!buf || buf->empty())
        return;
    size_t bytes = 0;
    if (!raw16ByteSize(*buf, bytes))
        return;
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    eraseRaw16Locked(key);
    if (m_raw16BudgetBytes == 0 || bytes > m_raw16BudgetBytes)
        return;
    while (!m_raw16Order.empty() && m_raw16Bytes > m_raw16BudgetBytes - bytes)
    {
        const std::string victim = m_raw16Order.back();
        eraseRaw16Locked(victim);
    }
    Raw16Entry e;
    e.buf = buf;
    e.channels = channels;
    e.maxSample = maxSample;
    m_raw16Store[key] = std::move(e);
    m_raw16Order.push_front(key);
    m_raw16Bytes += bytes;
}

bool CacheManager::getRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> &out,
                            int &channels, uint16_t &maxSample) const
{
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    auto it = m_raw16Store.find(key);
    if (it == m_raw16Store.end())
        return false;
    out = it->second.buf;
    channels = it->second.channels;
    maxSample = it->second.maxSample;
    m_raw16Order.remove(key);
    m_raw16Order.push_front(key);
    return true;
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
    {
        std::lock_guard<std::mutex> lock(m_raw16Mutex);
        eraseRaw16Locked(key);
    }
    DiskCache::instance().remove(key);
}

void CacheManager::eraseRaw16Locked(const std::string &key)
{
    const auto it = m_raw16Store.find(key);
    if (it == m_raw16Store.end())
        return;
    if (it->second.buf)
    {
        size_t bytes = 0;
        if (!raw16ByteSize(*it->second.buf, bytes))
            bytes = m_raw16Bytes;
        m_raw16Bytes = bytes > m_raw16Bytes ? 0 : m_raw16Bytes - bytes;
    }
    m_raw16Store.erase(it);
    m_raw16Order.remove(key);
}

void CacheManager::trimRaw16Locked()
{
    while (!m_raw16Order.empty() && m_raw16Bytes > m_raw16BudgetBytes)
    {
        const std::string victim = m_raw16Order.back();
        eraseRaw16Locked(victim);
    }
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
