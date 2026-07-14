#include "core/image/ImageCache.h"

ImageCache &ImageCache::instance() {
  static ImageCache inst;
  return inst;
}

ImageCache::ImageCache() {
  m_pools[Metadata].maxBytes = kMetaMaxBytes;
  m_pools[Thumbnail].maxBytes = kThumbMaxBytes;
  m_pools[Preview].maxBytes = kPreviewMaxBytes;
  m_pools[Viewer].maxBytes = kViewerMaxBytes;
}

void ImageCache::touch(Pool &pool, const std::string &key) {
  pool.order.remove(key);
  pool.order.push_front(key);
}

void ImageCache::evictIfNeeded(Pool &pool, size_t incoming) {
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

void ImageCache::put(Level level, const std::string &key,
                     const ImageData &img) {
  if (img.isNull())
    return;
  Pool &pool = m_pools[level];
  const size_t bytes = img.byteSize();
  std::lock_guard<std::mutex> lock(pool.mtx);
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

bool ImageCache::get(Level level, const std::string &key, ImageData &out) {
  Pool &pool = m_pools[level];
  std::lock_guard<std::mutex> lock(pool.mtx);
  auto it = pool.map.find(key);
  if (it == pool.map.end())
    return false;
  out = it->second.img;
  touch(pool, key);
  return true;
}

void ImageCache::remove(Level level, const std::string &key) {
  Pool &pool = m_pools[level];
  std::lock_guard<std::mutex> lock(pool.mtx);
  auto it = pool.map.find(key);
  if (it != pool.map.end()) {
    pool.curBytes -= it->second.bytes;
    pool.map.erase(it);
    pool.order.remove(key);
  }
}

void ImageCache::clear() {
  for (int i = 0; i < LevelCount; ++i) {
    Pool &pool = m_pools[i];
    std::lock_guard<std::mutex> lock(pool.mtx);
    pool.map.clear();
    pool.order.clear();
    pool.curBytes = 0;
  }
}

void ImageCache::setCapacity(Level level, size_t maxBytes) {
  Pool &pool = m_pools[level];
  std::lock_guard<std::mutex> lock(pool.mtx);
  pool.maxBytes = maxBytes;
}

size_t ImageCache::usedBytes(Level level) const {
  const Pool &pool = m_pools[level];
  std::lock_guard<std::mutex> lock(pool.mtx);
  return pool.curBytes;
}

size_t ImageCache::entryCount(Level level) const {
  const Pool &pool = m_pools[level];
  std::lock_guard<std::mutex> lock(pool.mtx);
  return pool.map.size();
}

size_t ImageCache::totalUsedBytes() const {
  size_t total = 0;
  for (int i = 0; i < LevelCount; ++i) {
    const Pool &pool = m_pools[i];
    std::lock_guard<std::mutex> lock(pool.mtx);
    total += pool.curBytes;
  }
  return total;
}
