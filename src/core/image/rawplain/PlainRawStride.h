#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/rawplain/PlainRawFile.h"

#include <cstdint>
#include <string>

// Internal: strided preview read for decodePlainRawFile. Not a public API.

namespace mviewer::core::rawplain
{

enum class StridePreviewStatus : std::uint8_t
{
    Unused, // Caller reads the full payload.
    Ready,  // `image` is the preview. Source params are unchanged.
    Failed  // `why` is set and `image` is empty.
};

// When maxEdge asks for a preview at least 2x smaller on both axes, read only
// the rows of a phase-preserving grid and render that grid. Does not re-probe
// Lsb16 depth and does not change `info`. Never throws.
StridePreviewStatus decodePlainRawStrided(const std::string &path, const PlainRawFileInfo &info,
                                          int maxEdge, ImageData &image, std::string &why);

} // namespace mviewer::core::rawplain
