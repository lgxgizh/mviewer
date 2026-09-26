#pragma once

#include "core/image/ImageFrame.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace mviewer::ui
{

// Bounded in-session RAM retain of decoded Compare frames (path + frameIndex).
// Complements ImageRepository / disk preload — does not replace them.
class CompareSessionFramePool
{
  public:
    static constexpr size_t kDefaultMaxEntries = 12;
    static constexpr size_t kDefaultMaxBytes = 384ull * 1024ull * 1024ull; // soft ~384 MiB

    explicit CompareSessionFramePool(size_t maxEntries = kDefaultMaxEntries,
                                     size_t maxBytes = kDefaultMaxBytes);

    void put(const std::string &path, int frameIndex, const std::shared_ptr<ImageFrame> &frame);
    std::shared_ptr<ImageFrame> tryGet(const std::string &path, int frameIndex);
    void touch(const std::string &path, int frameIndex);
    void clear();

    size_t size() const
    {
        return m_map.size();
    }
    size_t bytes() const
    {
        return m_bytes;
    }
    size_t maxEntries() const
    {
        return m_maxEntries;
    }
    size_t maxBytes() const
    {
        return m_maxBytes;
    }

    static std::string makeKey(const std::string &path, int frameIndex);

  private:
    struct Entry
    {
        std::string key;
        std::shared_ptr<ImageFrame> frame;
        size_t bytes = 0;
    };

    void evictWhileNeeded(size_t incomingBytes);
    static size_t estimateBytes(const std::shared_ptr<ImageFrame> &frame);

    size_t m_maxEntries;
    size_t m_maxBytes;
    size_t m_bytes = 0;
    std::list<Entry> m_lru; // front = MRU
    std::unordered_map<std::string, std::list<Entry>::iterator> m_map;
};

} // namespace mviewer::ui
