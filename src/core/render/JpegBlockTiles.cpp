#include "core/render/JpegBlockTiles.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <system_error>

namespace mviewer::core
{
namespace
{

constexpr std::size_t kMaxBlocks = 16; // ~64 MB of RGBA at 1024x1024

struct Block
{
    std::string key;
    std::mutex decodeMutex; // first caller decodes, concurrent callers wait
    bool decoded = false;
    ImageData pixels;
};

struct BlockCache
{
    std::mutex mutex;
    std::list<std::shared_ptr<Block>> lru; // front = most recent
};

BlockCache &cache()
{
    static BlockCache instance;
    return instance;
}

std::atomic<int> g_decodeCount{0};

std::string fileStamp(const std::string &path)
{
    namespace fs = std::filesystem;
    const fs::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec)
        return {};
    const auto mtime = fs::last_write_time(p, ec);
    if (ec)
        return {};
    return std::to_string(size) + ":" + std::to_string(mtime.time_since_epoch().count());
}

std::shared_ptr<Block> acquireBlock(const std::string &key)
{
    BlockCache &c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    const auto it = std::find_if(c.lru.begin(), c.lru.end(),
                                 [&key](const std::shared_ptr<Block> &b) { return b->key == key; });
    if (it != c.lru.end())
    {
        c.lru.splice(c.lru.begin(), c.lru, it);
        return c.lru.front();
    }
    auto block = std::make_shared<Block>();
    block->key = key;
    c.lru.push_front(block);
    while (c.lru.size() > kMaxBlocks)
        c.lru.pop_back();
    return block;
}

void dropBlock(const std::shared_ptr<Block> &block)
{
    BlockCache &c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    c.lru.remove(block);
}

bool hasJpegSuffix(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == "jpg" || ext == "jpeg";
}

} // namespace

bool jpegBlockEligible(const std::string &path, int srcX, int srcY, int srcW, int srcH, int targetW,
                       int targetH)
{
    if (path.empty() || srcX < 0 || srcY < 0 || srcW <= 0 || srcH <= 0)
        return false;
    if (srcW != targetW || srcH != targetH)
        return false;
    if (srcX / kJpegBlockEdge != (srcX + srcW - 1) / kJpegBlockEdge ||
        srcY / kJpegBlockEdge != (srcY + srcH - 1) / kJpegBlockEdge)
        return false;
    return hasJpegSuffix(path);
}

ImageData jpegBlockTile(const std::string &path, int srcX, int srcY, int srcW, int srcH,
                        const JpegBlockDecoder &decodeBlock)
{
    const std::string stamp = fileStamp(path);
    if (stamp.empty() || !decodeBlock)
        return {};
    const int bx = (srcX / kJpegBlockEdge) * kJpegBlockEdge;
    const int by = (srcY / kJpegBlockEdge) * kJpegBlockEdge;
    const std::string key =
        path + "|" + stamp + "|" + std::to_string(bx) + "|" + std::to_string(by);
    const std::shared_ptr<Block> block = acquireBlock(key);
    ImageData pixels;
    {
        const std::lock_guard<std::mutex> lock(block->decodeMutex);
        if (!block->decoded)
        {
            block->pixels = decodeBlock(bx, by, kJpegBlockEdge);
            block->decoded = true;
            g_decodeCount.fetch_add(1, std::memory_order_relaxed);
        }
        pixels = block->pixels;
    }
    if (pixels.isNull())
    {
        dropBlock(block); // do not pin a failure; the caller falls back
        return {};
    }
    ImageData tile =
        cropRegion(pixels, mviewer::domain::Selection{srcX - bx, srcY - by, srcW, srcH});
    if (tile.width != srcW || tile.height != srcH)
        return {};
    return tile;
}

void clearJpegBlockCache()
{
    BlockCache &c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    c.lru.clear();
    g_decodeCount.store(0, std::memory_order_relaxed);
}

int jpegBlockDecodeCount()
{
    return g_decodeCount.load(std::memory_order_relaxed);
}

} // namespace mviewer::core
