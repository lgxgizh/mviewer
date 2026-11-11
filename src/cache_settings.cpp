#include "cache_settings.h"

#include "core/image/DiskCache.h"
#include "thumbnailprovider.h"

#include <QSettings>

#include <algorithm>

namespace mviewer::ui
{
namespace
{
int clampedMb(const QSettings &settings, const char *key, int fallback, int minMb, int maxMb)
{
    return std::clamp(settings.value(QLatin1String(key), fallback).toInt(), minMb, maxMb);
}
} // namespace

void applyCacheSettings()
{
    QSettings settings;
    const int diskMb = clampedMb(settings, kDiskCacheMaxMbKey, kDiskCacheMbDefault, kDiskCacheMbMin,
                                 kDiskCacheMbMax);
    const int thumbMb = clampedMb(settings, kThumbCacheMaxMbKey, kThumbCacheMbDefault,
                                  kThumbCacheMbMin, kThumbCacheMbMax);
    // Only touch a cache whose cap changed: ThumbnailCache::setMaxBytes()
    // indexes the folder synchronously, which startup must not pay for.
    const size_t diskBytes = static_cast<size_t>(diskMb) * 1024u * 1024u;
    if (DiskCache::instance().maxBytes() != diskBytes)
        DiskCache::instance().setMaxBytes(diskBytes);
    const quint64 thumbBytes = static_cast<quint64>(thumbMb) * 1024u * 1024u;
    if (ThumbnailProvider::diskCacheMaxBytes() != thumbBytes)
        ThumbnailProvider::setDiskCacheMaxBytes(thumbBytes);
}

} // namespace mviewer::ui
