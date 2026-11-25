#pragma once

// Budgets for the hand-written HDR decoder. Shared with the core test so the
// rejection fixtures use the same ceilings the parser enforces.
//
// kMaxHdrEdge and kMaxHdrPixels are the existing image ceilings. This header
// does not raise them. kMaxExrInputBytes is the encoded size of one
// uncompressed RGB float image at that pixel ceiling, plus an offset table,
// per-scanline headers at the edge ceiling, and 1 MiB of attribute slack.
// A larger channel list can still be rejected by the input cap even when the
// pixel count alone would fit.

#include <cstddef>
#include <cstdint>

namespace mviewer::core::hdr_detail
{

constexpr int kMaxHdrEdge = 16384;
constexpr std::uint64_t kMaxHdrPixels = 64ull * 1024ull * 1024ull;
constexpr std::size_t kMaxExrChannels = 64;
constexpr std::size_t kMaxPfmHeaderLine = 4096;
constexpr std::uint64_t kMaxExrInputBytes =
    kMaxHdrPixels * 3ull * 4ull + static_cast<std::uint64_t>(kMaxHdrEdge) * 16ull + 1048576ull;

} // namespace mviewer::core::hdr_detail
