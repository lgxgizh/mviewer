#include "core/image/ImageCache.h"

ImageCache &ImageCache::instance()
{
    static ImageCache inst;
    return inst;
}

ImageCache::ImageCache()
{
    m_pools[Metadata].maxBytes = kMetaMaxBytes;
    m_pools[Thumbnail].maxBytes = kThumbMaxBytes;
    m_pools[Preview].maxBytes = kPreviewMaxBytes;
    m_pools[Viewer].maxBytes = kViewerMaxBytes;
}

void ImageCache::touch(Pool &pool, std::list<std::string>::iterator it)
{
    pool.order.splice(pool.order.begin(), pool.order, it);
}

void ImageCache::evictIfNeeded(Pool &pool, size_t incoming)
{
    while (pool.curBytes + incoming > pool.maxBytes && !pool.order.empty())
    {
        const std::string victim = std::move(pool.order.back());
        pool.order.pop_back();
        auto it = pool.map.find(victim);
        if (it != pool.map.end())
        {
            pool.curBytes -= it->second.bytes;
            pool.map.erase(it);
        }
    }
}

void ImageCache::put(Level level, const std::string &key, const ImageData &img)
{
    if (img.isNull() || key.empty())
        return;
    Pool &pool = m_pools[level];
    const size_t bytes = img.byteSize();
    std::lock_guard<std::mutex> lock(pool.mtx);

    // Refuse an entry larger than the pool capacity BEFORE evicting.
    // Calling evictIfNeeded first would evict all existing cached items
    // before refusing the oversized entry, wiping the entire pool.
    if (pool.maxBytes > 0 && bytes > pool.maxBytes)
        return;

    auto it = pool.map.find(key);
    if (it != pool.map.end())
    {
        pool.curBytes -= it->second.bytes;
        pool.order.erase(it->second.orderIt);
        pool.map.erase(it);
    }
    evictIfNeeded(pool, bytes);

    pool.order.push_front(key);
    Entry e{img, bytes, pool.order.begin()};
    pool.map.emplace(key, std::move(e));
    pool.curBytes += bytes;
}

bool ImageCache::get(Level level, const std::string &key, ImageData &out)
{
    if (key.empty())
        return false;
    Pool &pool = m_pools[level];
    std::lock_guard<std::mutex> lock(pool.mtx);
    auto it = pool.map.find(key);
    if (it == pool.map.end())
        return false;
    out = it->second.img;
    touch(pool, it->second.orderIt);
    return true;
}

void ImageCache::remove(Level level, const std::string &key)
{
    if (key.empty())
        return;
    Pool &pool = m_pools[level];
    std::lock_guard<std::mutex> lock(pool.mtx);
    auto it = pool.map.find(key);
    if (it != pool.map.end())
    {
        pool.curBytes -= it->second.bytes;
        pool.order.erase(it->second.orderIt);
        pool.map.erase(it);
    }
}

void ImageCache::clear()
{
    for (int i = 0; i < LevelCount; ++i)
    {
        Pool &pool = m_pools[i];
        std::lock_guard<std::mutex> lock(pool.mtx);
        pool.map.clear();
        pool.order.clear();
        pool.curBytes = 0;
    }
}

void ImageCache::setCapacity(Level level, size_t maxBytes)
{
    Pool &pool = m_pools[level];
    std::lock_guard<std::mutex> lock(pool.mtx);
    pool.maxBytes = maxBytes;
    // Shrinking the budget must take effect immediately: without the trim the
    // pool stayed above its documented cap until enough new entries arrived.
    evictIfNeeded(pool, 0);
}

size_t ImageCache::usedBytes(Level level) const
{
    const Pool &pool = m_pools[level];
    std::lock_guard<std::mutex> lock(pool.mtx);
    return pool.curBytes;
}

size_t ImageCache::entryCount(Level level) const
{
    const Pool &pool = m_pools[level];
    std::lock_guard<std::mutex> lock(pool.mtx);
    return pool.map.size();
}

size_t ImageCache::totalUsedBytes() const
{
    size_t total = 0;
    for (int i = 0; i < LevelCount; ++i)
    {
        const Pool &pool = m_pools[i];
        std::lock_guard<std::mutex> lock(pool.mtx);
        total += pool.curBytes;
    }
    return total;
}
