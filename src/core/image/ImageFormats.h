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
#include <string_view>
#include <vector>

namespace mviewer::core
{

class ImageFormats
{
  public:
    // Every suffix a registered decoder accepts, lowercased, WITHOUT the dot
    // (e.g. {"jpg","jpeg","png","webp","gif","cr2","dng",...}). Returned BY
    // VALUE: the set is rebuilt when the decoder registry or the presence of a
    // QCoreApplication changes, and a caller holding a reference to shared
    // mutable state could observe it being rebuilt under an iteration.
    static std::vector<std::string> supportedSuffixes();

    // True when `suffix` (case-insensitive, with or without leading dot) is a
    // supported image extension.
    static bool isSupportedSuffix(const std::string &suffix);

    // True when `path` has a supported image extension.
    static bool isSupportedPath(const std::string &path);

    // "*.jpg" wildcard form (Qt QDir / QFileDialog filter convention).
    static std::vector<std::string> wildcardFilters();
};

// Single RAW-extension catalog. Every RAW gate (decoder, metadata, type
// filter, UI combo) must use this list. Entries are lowercase, no dot, sorted.
const std::vector<std::string_view> &rawExtensions();

// Case-insensitive. Accepts "CR2", ".cr2", or "cr2".
bool isRawExtension(std::string_view extNoDot);

// True when the path's final extension is a RAW extension.
bool isRawPath(std::string_view path);

// Comma-joined rawExtensions(), used as the type-filter combo payload.
std::string rawTypeFilterData();

// Empty filter matches every suffix. Token "raw" matches rawExtensions().
// Token "tiff" matches tif and tiff. Other tokens match exactly.
bool suffixMatchesTypeFilter(std::string_view suffixNoDot, std::string_view typeFilterCsv);

} // namespace mviewer::core
