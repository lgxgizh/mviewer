#pragma once

#include "ImageBuffer.h"
#include "core/image/DiskCache.h"

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

// 多级内存缓存（分层缓存的内存部分）。按用途分四档（均为
// LRU，各自独立的容量/淘汰/生命周期）：
//  - Metadata：极小图/文件级派生数据（数量多、占用极小，字节受限）
//  - Thumbnail：小图（列表/画廊），数量多、占用小
//  - Preview  ：中图（左栏预览），数量中
//  - Viewer   ：全分辨率解码结果（双击大图/比较），数量少、占用大
// 每档拥有自己的互斥锁（非全局锁），由 CacheManager 统一调度与统计。
// 注意：接口只暴露 std::string 键与 ImageData 值，不依赖 Qt 类型。
class ImageCache {
public:
  enum Level { Metadata, Thumbnail, Preview, Viewer, LevelCount };

  static ImageCache &instance();

  void put(Level level, const std::string &key, const ImageData &img);
  bool get(Level level, const std::string &key, ImageData &out);

  // 删除单条条目（仓库释放/失效时使用）。
  void remove(Level level, const std::string &key);

  void clear();

  // 设置某一级的容量上限（字节）。CacheManager 在构造成立后应用其 CacheConfig。
  void setCapacity(Level level, size_t maxBytes);

  // 每级已用字节 / 条目数（供 CacheManager 做分层统计）。
  size_t usedBytes(Level level) const;
  size_t entryCount(Level level) const;

  // 所有内存池的已用字节总量
  size_t totalUsedBytes() const;

private:
  ImageCache();

  struct Entry {
    ImageData img;
    size_t bytes = 0;
  };

  struct Pool {
    size_t maxBytes = 0;
    size_t curBytes = 0;
    std::list<std::string> order; // 最近使用在前
    std::unordered_map<std::string, Entry> map;
    mutable std::mutex mtx;
  };

  void evictIfNeeded(Pool &pool, size_t incoming);
  void touch(Pool &pool, const std::string &key);

  Pool m_pools[LevelCount];

  static constexpr size_t kMetaMaxBytes = 16 * 1024 * 1024;     // 16MB
  static constexpr size_t kThumbMaxBytes = 64 * 1024 * 1024;    // 64MB
  static constexpr size_t kPreviewMaxBytes = 256 * 1024 * 1024; // 256MB
  static constexpr size_t kViewerMaxBytes = 512 * 1024 * 1024;  // 512MB
};
