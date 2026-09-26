#include "compare_session_frame_pool.h"

#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <string>

namespace mviewer::ui
{

CompareSessionFramePool::CompareSessionFramePool(size_t maxEntries, size_t maxBytes)
    : m_maxEntries(std::max<size_t>(1, maxEntries)), m_maxBytes(std::max<size_t>(1, maxBytes))
{
}

std::string CompareSessionFramePool::makeKey(const std::string &path, int frameIndex)
{
    return path + "#" + std::to_string(std::max(0, frameIndex));
}

size_t CompareSessionFramePool::estimateBytes(const std::shared_ptr<ImageFrame> &frame)
{
    if (!frame)
        return 0;
    const ImageData &pixels = frame->pixels();
    if (pixels.isNull())
        return 64; // metadata-only placeholder
    return pixels.byteSize() + 256;
}

void CompareSessionFramePool::evictWhileNeeded(size_t incomingBytes)
{
    while (!m_lru.empty() && (m_map.size() >= m_maxEntries || m_bytes + incomingBytes > m_maxBytes))
    {
        Entry &victim = m_lru.back();
        m_bytes -= victim.bytes;
        m_map.erase(victim.key);
        m_lru.pop_back();
    }
}

void CompareSessionFramePool::put(const std::string &path, int frameIndex,
                                  const std::shared_ptr<ImageFrame> &frame)
{
    if (path.empty() || !frame)
        return;
    // Skip empty placeholders — they are cheap to recreate and waste pool slots.
    if (frame->pixels().isNull())
        return;

    const std::string key = makeKey(path, frameIndex);
    const size_t bytes = estimateBytes(frame);

    auto it = m_map.find(key);
    if (it != m_map.end())
    {
        m_bytes -= it->second->bytes;
        m_lru.erase(it->second);
        m_map.erase(it);
    }

    evictWhileNeeded(bytes);
    // If a single frame exceeds the budget, still keep it alone (best-effort).
    if (bytes > m_maxBytes)
    {
        m_lru.clear();
        m_map.clear();
        m_bytes = 0;
    }

    m_lru.push_front(Entry{key, frame, bytes});
    m_map[key] = m_lru.begin();
    m_bytes += bytes;
}

std::shared_ptr<ImageFrame> CompareSessionFramePool::tryGet(const std::string &path, int frameIndex)
{
    const std::string key = makeKey(path, frameIndex);
    auto it = m_map.find(key);
    if (it == m_map.end())
        return {};
    m_lru.splice(m_lru.begin(), m_lru, it->second);
    return it->second->frame;
}

void CompareSessionFramePool::touch(const std::string &path, int frameIndex)
{
    (void)tryGet(path, frameIndex);
}

void CompareSessionFramePool::clear()
{
    m_lru.clear();
    m_map.clear();
    m_bytes = 0;
}

} // namespace mviewer::ui
