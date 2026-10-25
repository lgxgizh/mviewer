#include "core/image/decoder/RawDecodePlan.h"

#include "core/image/ImageFormats.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace mviewer::core
{

namespace
{

std::atomic<int> g_policy{static_cast<int>(RawDisplayPolicy::PreferPreview)};
std::atomic<int> g_fullCount{0};
std::mutex g_mutex;
std::unordered_set<std::string> g_fullPaths;
std::unordered_map<std::string, mviewer::domain::RawSource> g_sourceByKey;

bool keyHasRawMarker(const std::string &key)
{
    return key.find("|rawpol=") != std::string::npos || key.find("|rawfull=") != std::string::npos;
}

} // namespace

void setRawDisplayPolicy(RawDisplayPolicy policy)
{
    int value = static_cast<int>(policy);
    if (value < 0 || value > static_cast<int>(RawDisplayPolicy::AlwaysFull))
        value = static_cast<int>(RawDisplayPolicy::PreferPreview);
    g_policy.store(value, std::memory_order_relaxed);
}

RawDisplayPolicy rawDisplayPolicy()
{
    return rawDisplayPolicyFromInt(g_policy.load(std::memory_order_relaxed));
}

void requestRawFullDecode(const std::string &path)
{
    if (!isRawPath(path))
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_fullPaths.insert(path).second)
        g_fullCount.fetch_add(1, std::memory_order_relaxed);
}

void clearRawFullDecode(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_fullPaths.erase(path) > 0)
        g_fullCount.fetch_sub(1, std::memory_order_relaxed);
}

bool rawFullDecodeRequested(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_fullPaths.find(path) != g_fullPaths.end();
}

std::string rawCacheKeySuffix(const std::string &path)
{
    const RawDisplayPolicy policy = rawDisplayPolicy();
    if (policy == RawDisplayPolicy::PreferPreview &&
        g_fullCount.load(std::memory_order_relaxed) == 0)
        return {};
    if (!isRawPath(path))
        return {};
    const bool full = rawFullDecodeRequested(path);
    if (policy == RawDisplayPolicy::PreferPreview && !full)
        return {};
    std::string suffix;
    if (policy != RawDisplayPolicy::PreferPreview)
        suffix += "|rawpol=" + std::to_string(static_cast<int>(policy));
    if (full)
        suffix += "|rawfull=1";
    return suffix;
}

bool rawCacheKeyAgrees(const std::string &key, const std::string &suffix)
{
    if (suffix.empty())
        return !keyHasRawMarker(key);
    if (key.size() < suffix.size() ||
        key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;
    return !keyHasRawMarker(key.substr(0, key.size() - suffix.size()));
}

void rememberRawSource(const std::string &cacheKey, mviewer::domain::RawSource source)
{
    if (cacheKey.empty() || source == mviewer::domain::RawSource::NotRaw)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sourceByKey[cacheKey] = source;
}

mviewer::domain::RawSource recalledRawSource(const std::string &cacheKey)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_sourceByKey.find(cacheKey);
    if (it == g_sourceByKey.end())
        return mviewer::domain::RawSource::NotRaw;
    return it->second;
}

} // namespace mviewer::core
