#pragma once
// M25: sort keys for the Browse gallery.
//
// Resolution / Camera / Lens sorts used to run their expensive reads INSIDE
// std::sort comparators — O(N log N) header parses per sort. This module
// computes one key per file (O(N) I/O) and leaves the comparator pure memory.
//
// Qt-free header; the .cpp may use Qt (QImageReader, QFileInfo).

#include "core/image/RawMetadata.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mviewer::core
{

enum class SortField
{
    Name,
    Date,
    Size,
    Resolution,
    Type,
    Rating,
    Camera,
    Lens
};

// Precomputed per-file sort key (fields not needed for the requested field
// stay zeroed — a name sort performs no file I/O at all).
struct ImageSortKey
{
    std::string path;
    int64_t size = 0;
    int64_t mtimeSec = 0;
    std::string suffix;
    int64_t resolution = 0; // width * height
    std::string camera;     // "make model" (lowercased)
    std::string lens;       // lens model (lowercased)
    int rating = 0;
};

// The two expensive readers, injected so tests can COUNT the I/O:
//   dimension reader returns the pixel count of a path;
//   metadata reader returns the RAW metadata of a path.
using SortDimensionReader = std::function<int64_t(const std::string &path)>;
using SortMetadataReader = std::function<RawMetadata(const std::string &path)>;

// Compute sort keys for `paths` at field `field`. Reads each file at most
// once (and only the reads `field` actually needs); the returned keys are
// then sorted in memory — never parse inside a comparator.
std::vector<ImageSortKey> computeSortKeys(const std::vector<std::string> &paths, SortField field,
                                          const SortDimensionReader &dimensionReader = {},
                                          const SortMetadataReader &metadataReader = {});

} // namespace mviewer::core
