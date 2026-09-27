#include "core/image/DisplayMip.h"

#include "core/cache/CacheManager.h"
#include "core/image/MetadataReader.h"

namespace mviewer::core
{
namespace
{

bool lookup(const std::string &key, int maxEdge, ImageData &out)
{
    if (key.empty() || maxEdge <= 0)
        return false;
    return CacheManager::instance().getBestMip(key, maxEdge, out) && !out.isNull();
}

} // namespace

ImageData tryBestMip(const std::string &path, int maxEdge)
{
    ImageData out;
    if (lookup(path, maxEdge, out))
        return out;
    const std::string keyed = MetadataReader::key(path);
    if (!keyed.empty() && keyed != path && lookup(keyed, maxEdge, out))
        return out;
    return ImageData{};
}

ImageData ensureAndBestMip(const std::string &path, const ImageData &full, int maxEdge)
{
    if (path.empty() || full.isNull() || maxEdge <= 0)
        return ImageData{};
    CacheManager::instance().ensureMips(path, full);
    return tryBestMip(path, maxEdge);
}

void dropMips(const std::string &baseKey)
{
    CacheManager::instance().dropMips(baseKey);
}

size_t trimMipsToBudget(size_t maxBytes, const std::vector<std::string> &keepBaseKeys)
{
    return CacheManager::instance().trimMipsToBudget(maxBytes, keepBaseKeys);
}

} // namespace mviewer::core
