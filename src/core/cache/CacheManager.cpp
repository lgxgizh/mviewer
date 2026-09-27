#include "core/cache/CacheManager.h"
#include "core/cache/MipmapPyramid.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

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

std::string CacheManager::mipKey(const std::string &baseKey, int lod)
{
    return baseKey + "#mip:" + std::to_string(lod);
}

void CacheManager::putMemory(CacheLevel level, const std::string &key, const ImageData &img)
{
    if (level == CacheLevel::Disk)
        return;
    ImageCache::instance().put(toImageCacheLevel(level), key, img);
    // Eager mip build for FullImage puts (lod≥1 live in Preview pool).
    // Replace path: drop prior chain so a new full raster cannot leave stale mips.
    if (level == CacheLevel::FullImage && !key.empty() && !img.isNull())
    {
        eraseMips(key);
        ensureMips(key, img);
    }
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
        recordMiss(level);
        recordHit(CacheLevel::Disk);
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

void CacheManager::eraseMips(const std::string &baseKey)
{
    if (baseKey.empty())
        return;
    int maxLod = 0;
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        const auto it = m_mipChains.find(baseKey);
        if (it == m_mipChains.end())
            return;
        maxLod = it->second.maxLod;
        m_mipChains.erase(it);
    }
    for (int lod = 1; lod <= maxLod; ++lod)
        ImageCache::instance().remove(ImageCache::Preview, mipKey(baseKey, lod));
}

void CacheManager::dropMips(const std::string &baseKey)
{
    eraseMips(baseKey);
}

size_t CacheManager::trimMipsToBudget(size_t maxBytes,
                                      const std::vector<std::string> &keepBaseKeys)
{
    std::unordered_set<std::string> keep(keepBaseKeys.begin(), keepBaseKeys.end());
    struct Cold
    {
        std::string key;
        size_t bytes = 0;
    };
    std::vector<Cold> cold;
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        cold.reserve(m_mipChains.size());
        for (const auto &entry : m_mipChains)
        {
            total += entry.second.bytes;
            if (keep.count(entry.first) != 0)
                continue;
            cold.push_back(Cold{entry.first, entry.second.bytes});
        }
    }
    std::sort(cold.begin(), cold.end(),
              [](const Cold &a, const Cold &b) { return a.bytes > b.bytes; });
    size_t released = 0;
    for (const Cold &entry : cold)
    {
        if (total <= maxBytes)
            break;
        dropMips(entry.key);
        const size_t drop = entry.bytes > total ? total : entry.bytes;
        total -= drop;
        released += drop;
    }
    return released;
}

void CacheManager::storeMipChain(const std::string &baseKey, const std::vector<ImageData> &levels)
{
    if (baseKey.empty() || levels.size() <= 1)
        return;
    const int maxLod = static_cast<int>(levels.size()) - 1;
    size_t bytes = 0;
    for (int lod = 1; lod <= maxLod; ++lod)
    {
        const ImageData &img = levels[static_cast<size_t>(lod)];
        if (img.isNull())
            continue;
        ImageCache::instance().put(ImageCache::Preview, mipKey(baseKey, lod), img);
        bytes += img.byteSize();
    }
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        m_mipChains[baseKey] = MipChainInfo{maxLod, bytes};
    }
}

int CacheManager::ensureMips(const std::string &baseKey, const ImageData &full, int minEdge)
{
    if (baseKey.empty() || full.isNull())
        return 0;
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        if (m_mipChains.count(baseKey) != 0)
        {
            // Already tracked — still count levels as 1 + maxLod.
            return 1 + m_mipChains[baseKey].maxLod;
        }
    }
    auto levels = mviewer::cache::buildMipChain(full, minEdge);
    if (levels.empty())
        return 0;
    storeMipChain(baseKey, levels);
    return static_cast<int>(levels.size());
}

void CacheManager::putMip(const std::string &baseKey, int lod, const ImageData &img)
{
    if (baseKey.empty() || img.isNull() || lod < 0)
        return;
    if (lod == 0)
    {
        ImageCache::instance().put(ImageCache::Viewer, baseKey, img);
        return;
    }
    ImageCache::instance().put(ImageCache::Preview, mipKey(baseKey, lod), img);
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        MipChainInfo &info = m_mipChains[baseKey];
        if (lod > info.maxLod)
            info.maxLod = lod;
        info.bytes += img.byteSize();
    }
}

bool CacheManager::getMip(const std::string &baseKey, int lod, ImageData &out)
{
    if (baseKey.empty() || lod < 0)
        return false;
    if (lod == 0)
        return ImageCache::instance().get(ImageCache::Viewer, baseKey, out);
    return ImageCache::instance().get(ImageCache::Preview, mipKey(baseKey, lod), out);
}

bool CacheManager::getBestMipExisting(const std::string &baseKey, int maxEdge, ImageData &out)
{
    if (baseKey.empty() || maxEdge <= 0)
        return false;

    int trackedMax = 0;
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        const auto it = m_mipChains.find(baseKey);
        if (it != m_mipChains.end())
            trackedMax = it->second.maxLod;
    }

    // Prefer the largest level whose max edge ≤ maxEdge (coarse enough).
    ImageData bestFit;
    int bestFitEdge = -1;
    ImageData nextLarger;
    int nextLargerEdge = std::numeric_limits<int>::max();

    auto consider = [&](const ImageData &img)
    {
        if (img.isNull())
            return;
        const int edge = mviewer::cache::imageMaxEdge(img);
        if (edge <= maxEdge)
        {
            if (edge > bestFitEdge)
            {
                bestFit = img;
                bestFitEdge = edge;
            }
        }
        else if (edge < nextLargerEdge)
        {
            nextLarger = img;
            nextLargerEdge = edge;
        }
    };

    ImageData full;
    if (ImageCache::instance().get(ImageCache::Viewer, baseKey, full))
        consider(full);

    for (int lod = 1; lod <= trackedMax; ++lod)
    {
        ImageData mip;
        if (ImageCache::instance().get(ImageCache::Preview, mipKey(baseKey, lod), mip))
            consider(mip);
    }

    if (!bestFit.isNull())
    {
        out = std::move(bestFit);
        return true;
    }
    if (!nextLarger.isNull())
    {
        out = std::move(nextLarger);
        return true;
    }
    return false;
}

bool CacheManager::getBestMip(const std::string &baseKey, int maxEdge, ImageData &out)
{
    if (baseKey.empty() || maxEdge <= 0)
        return false;

    int trackedMax = 0;
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        const auto it = m_mipChains.find(baseKey);
        if (it != m_mipChains.end())
            trackedMax = it->second.maxLod;
    }

    ImageData full;
    const bool haveFull =
        ImageCache::instance().get(ImageCache::Viewer, baseKey, full) && !full.isNull();

    // Lazy fill when we only have a too-large FullImage (no smaller mips yet).
    if (trackedMax == 0 && haveFull && mviewer::cache::imageMaxEdge(full) > maxEdge)
        ensureMips(baseKey, full);

    if (getBestMipExisting(baseKey, maxEdge, out))
        return true;

    // Still nothing: if FullImage exists but was never considered (edge==0 etc.)
    if (haveFull)
    {
        out = std::move(full);
        return true;
    }
    return false;
}

void CacheManager::erase(const std::string &key)
{
    if (key.empty())
        return;
    eraseMips(key);
    ImageCache::instance().remove(ImageCache::Metadata, key);
    ImageCache::instance().remove(ImageCache::Thumbnail, key);
    ImageCache::instance().remove(ImageCache::Preview, key);
    ImageCache::instance().remove(ImageCache::Viewer, key);
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        auto it = m_metaStore.find(key);
        if (it != m_metaStore.end())
        {
            m_metaOrder.erase(it->second.orderIt);
            m_metaStore.erase(it);
        }
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
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_metaStore.clear();
        m_metaOrder.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_raw16Mutex);
        m_raw16Store.clear();
        m_raw16Order.clear();
        m_raw16Bytes = 0;
    }
    {
        std::lock_guard<std::mutex> lock(m_mipMutex);
        m_mipChains.clear();
    }
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
    if (key.empty())
        return;
    std::lock_guard<std::mutex> lock(m_metaMutex);
    auto it = m_metaStore.find(key);
    if (it != m_metaStore.end())
    {
        it->second.meta = meta;
        m_metaOrder.splice(m_metaOrder.begin(), m_metaOrder, it->second.orderIt);
        return;
    }
    if (m_metaStore.size() >= kMetaMaxEntries && !m_metaOrder.empty())
    {
        const std::string victim = std::move(m_metaOrder.back());
        m_metaOrder.pop_back();
        m_metaStore.erase(victim);
    }
    m_metaOrder.push_front(key);
    MetaEntry e;
    e.meta = meta;
    e.orderIt = m_metaOrder.begin();
    m_metaStore.emplace(key, std::move(e));
}

bool CacheManager::getMetadata(const std::string &key, mviewer::domain::ImageMetadata &out) const
{
    if (key.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_metaMutex);
    auto it = m_metaStore.find(key);
    if (it == m_metaStore.end())
        return false;
    out = it->second.meta;
    m_metaOrder.splice(m_metaOrder.begin(), m_metaOrder, it->second.orderIt);
    return true;
}

bool CacheManager::hasMetadata(const std::string &key) const
{
    if (key.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_metaMutex);
    return m_metaStore.find(key) != m_metaStore.end();
}

void CacheManager::putRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> buf,
                            int channels, uint16_t maxSample)
{
    if (key.empty() || !buf || buf->empty())
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
    m_raw16Order.push_front(key);
    Raw16Entry e;
    e.buf = buf;
    e.channels = channels;
    e.maxSample = maxSample;
    e.orderIt = m_raw16Order.begin();
    m_raw16Store[key] = std::move(e);
    m_raw16Bytes += bytes;
}

bool CacheManager::getRaw16(const std::string &key, std::shared_ptr<std::vector<uint16_t>> &out,
                            int &channels, uint16_t &maxSample) const
{
    if (key.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_raw16Mutex);
    auto it = m_raw16Store.find(key);
    if (it == m_raw16Store.end())
        return false;
    out = it->second.buf;
    channels = it->second.channels;
    maxSample = it->second.maxSample;
    m_raw16Order.splice(m_raw16Order.begin(), m_raw16Order, it->second.orderIt);
    return true;
}

void CacheManager::invalidate(const std::string &key)
{
    if (key.empty())
        return;
    eraseMips(key);
    ImageCache::instance().remove(ImageCache::Metadata, key);
    ImageCache::instance().remove(ImageCache::Thumbnail, key);
    ImageCache::instance().remove(ImageCache::Preview, key);
    ImageCache::instance().remove(ImageCache::Viewer, key);
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        auto it = m_metaStore.find(key);
        if (it != m_metaStore.end())
        {
            m_metaOrder.erase(it->second.orderIt);
            m_metaStore.erase(it);
        }
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
    m_raw16Order.erase(it->second.orderIt);
    m_raw16Store.erase(it);
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
