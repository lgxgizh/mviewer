#pragma once

namespace mviewer::ui
{

constexpr int kDiskCacheMbMin = 256;
constexpr int kDiskCacheMbMax = 16384;
constexpr int kDiskCacheMbStep = 256;
constexpr int kDiskCacheMbDefault = 2048;

constexpr int kThumbCacheMbMin = 64;
constexpr int kThumbCacheMbMax = 4096;
constexpr int kThumbCacheMbStep = 64;
constexpr int kThumbCacheMbDefault = 512;

inline constexpr const char *kDiskCacheMaxMbKey = "diskCacheMaxMB";
inline constexpr const char *kThumbCacheMaxMbKey = "thumbnailCacheMaxMB";

// Reads diskCacheMaxMB and thumbnailCacheMaxMB (defaults 2048 / 512, clamped
// to the preference ranges) and applies them to DiskCache and ThumbnailCache.
void applyCacheSettings();

} // namespace mviewer::ui
