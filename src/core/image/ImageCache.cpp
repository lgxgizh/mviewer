#include "core/image/ImageCache.h"

ImageCache &ImageCache::instance()
{
    static ImageCache inst;
    return inst;
}

ImageCache::ImageCache()
{
    m_pools[Thumbnail].maxBytes = kThumbMaxBytes;
    m_pools[Preview].maxBytes = kPreviewMaxBytes;
    m_pools[Viewer].maxBytes = kViewerMaxBytes;
}

void ImageCache::touch(Pool &pool, const std::string &key)
{
    pool.order.remove(key);
    pool.order.push_front(key);
}

void ImageCache::evictIfNeeded(Pool &pool, size_t incoming)
{
    while (pool.curBytes + incoming > pool.maxBytes && !pool.order.empty()) {
        const std::string victim = pool.order.back();
        pool.order.pop_back();
        auto it = pool.map.find(victim);
        if (it != pool.map.end()) {
            pool.curBytes -= it->second.bytes;
            pool.map.erase(it);
        }
    }
}

void ImageCache::put(Level level, const std::string &key, const ImageData &img)
{
    if (img.isNull())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    Pool &pool = m_pools[level];
    const size_t bytes = img.byteSize();

    auto it = pool.map.find(key);
    if (it != pool.map.end()) {
        pool.curBytes -= it->second.bytes;
        pool.map.erase(it);
        pool.order.remove(key);
    }
    evictIfNeeded(pool, bytes);
    Entry e{img, bytes};
    pool.map.emplace(key, std::move(e));
    pool.order.push_front(key);
    pool.curBytes += bytes;
}

bool ImageCache::get(Level level, const std::string &key, ImageData &out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Pool &pool = m_pools[level];
    auto it = pool.map.find(key);
    if (it == pool.map.end())
        return false;
    out = it->second.img;
    touch(pool, key);
    return true;
}

void ImageCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < 3; ++i) {
        m_pools[i].map.clear();
        m_pools[i].order.clear();
        m_pools[i].curBytes = 0;
    }
}
