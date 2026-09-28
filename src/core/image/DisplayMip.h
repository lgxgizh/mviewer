#pragma once

#include "core/image/ImageBuffer.h"

#include <string>
#include <vector>

namespace mviewer::core
{

// Thin facade so UI/Compare can prefer CacheManager mip levels without
// including CacheManager / ImageRepository (architecture_gate R1/R2).
// Tries `path` then the repository-style MetadataReader::key(path).

ImageData tryBestMip(const std::string &path, int maxEdge);

// Build/store mips under `path` from an already-decoded full raster, then
// return tryBestMip(path, maxEdge). Empty on failure.
ImageData ensureAndBestMip(const std::string &path, const ImageData &full, int maxEdge);

// Drop Preview-pool lod≥1 entries for one base key. Lod 0, Thumbnail,
// Metadata, and non-mip Preview keys stay. Not a global clearMemory.
void dropMips(const std::string &baseKey);

// Drop cold lod≥1 chains (largest first) until tracked mip bytes fit maxBytes.
// keepBaseKeys stay resident. Returns the tracked bytes released.
size_t trimMipsToBudget(size_t maxBytes, const std::vector<std::string> &keepBaseKeys = {});

} // namespace mviewer::core
