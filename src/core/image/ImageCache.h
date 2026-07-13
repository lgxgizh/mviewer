#pragma once

#include "ImageBuffer.h"

#include <mutex>
#include <list>
#include <unordered_map>
#include <string>

// 多级内存缓存。按用途分三档（均为 LRU）：
//  - Thumbnail：小图（列表/画廊），数量多、占用小
//  - Preview  ：中图（左栏预览），数量中
//  - Viewer   ：全分辨率解码结果（双击大图/比较），数量少、占用大
// 全部线程安全（由 Scheduler 的池线程读写，GUI 线程读）。
// 注意：接口只暴露 std::string 键与 ImageData 值，不依赖 Qt 类型。
class ImageCache
{
public:
    enum Level { Thumbnail, Preview, Viewer };

    static ImageCache &instance();

    void put(Level level, const std::string &key, const ImageData &img);
    bool get(Level level, const std::string &key, ImageData &out);

    void clear();

private:
    ImageCache();

    struct Entry
    {
        ImageData img;
        size_t bytes = 0;
    };

    struct Pool
    {
        size_t maxBytes = 0;
        size_t curBytes = 0;
        std::list<std::string> order; // 最近使用在前
        std::unordered_map<std::string, Entry> map;
    };

    void evictIfNeeded(Pool &pool, size_t incoming);
    void touch(Pool &pool, const std::string &key);

    std::mutex m_mutex;
    Pool m_pools[3];

    static constexpr size_t kThumbMaxBytes = 256 * 1024 * 1024;  // 256MB
    static constexpr size_t kPreviewMaxBytes = 512 * 1024 * 1024; // 512MB
    static constexpr size_t kViewerMaxBytes = 1024 * 1024 * 1024; // 1GB
};
