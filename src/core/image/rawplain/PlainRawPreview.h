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

void renderPreview(const Params &p, const std::vector<std::uint16_t> &samples,
                   const std::array<std::vector<std::uint16_t>, 4> &luts, const std::uint8_t *enc,
                   int outW, int outH, std::uint8_t *dst);

} // namespace detail
} // namespace mviewer::core::rawplain
