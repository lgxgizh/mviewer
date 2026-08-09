#pragma once
// M25: supported-format SSOT for the Browse workflow.
//
// Before this helper the shipped formats were enumerated in four divergent
// places: FileSystem (6 formats — no WebP/GIF/RAW), ThumbnailPanel's suffix
// list, the recursive filename search and the panel's directory sort. RAW +
// WebP + GIF directories therefore counted differently in the gallery, the
// status bar, navigation and search. The DecoderRegistry is the authoritative
// set of decodable formats; this helper exposes it to every format decision.
//
// Qt-free header; the .cpp may use Qt/DecoderRegistry.

#include <string>
#include <vector>

namespace mviewer::core
{

class ImageFormats
{
  public:
    // Every suffix a registered decoder accepts, lowercased, WITHOUT the dot
    // (e.g. {"jpg","jpeg","png","webp","gif","cr2","dng",...}).
    static const std::vector<std::string> &supportedSuffixes();

    // True when `suffix` (case-insensitive, with or without leading dot) is a
    // supported image extension.
    static bool isSupportedSuffix(const std::string &suffix);

    // True when `path` has a supported image extension.
    static bool isSupportedPath(const std::string &path);

    // "*.jpg" wildcard form (Qt QDir / QFileDialog filter convention).
    static std::vector<std::string> wildcardFilters();
};

} // namespace mviewer::core
