#pragma once

#include "core/image/ImageBuffer.h"

#include <string>

namespace mviewer::core
{

// Thin facade so UI/Compare can prefer CacheManager mip levels without
// including CacheManager / ImageRepository (architecture_gate R1/R2).
// Tries `path` then the repository-style MetadataReader::key(path).

ImageData tryBestMip(const std::string &path, int maxEdge);

// Build/store mips under `path` from an already-decoded full raster, then
// return tryBestMip(path, maxEdge). Empty on failure.
ImageData ensureAndBestMip(const std::string &path, const ImageData &full, int maxEdge);

} // namespace mviewer::core
