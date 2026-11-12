#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

#include <array>
#include <cstdint>
#include <vector>

// Internal helpers shared by the rawplain render translation units.
// Not part of the public rawplain API.

namespace mviewer::core::rawplain
{
namespace detail
{

// Output size of the fast preview. False when maxEdge <= 0 or the frame
// already fits, and then outW/outH stay w/h (the caller renders 1:1).
// Otherwise the longest edge is maxEdge, aspect is kept, and each side is
// at least 1.
bool previewDimensions(int w, int h, int maxEdge, int &outW, int &outH);

void renderPreview(const Params &p, const std::vector<std::uint16_t> &samples,
                   const std::array<std::vector<std::uint16_t>, 4> &luts, const std::uint8_t *enc,
                   int outW, int outH, std::uint8_t *dst);

} // namespace detail
} // namespace mviewer::core::rawplain
