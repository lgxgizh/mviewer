#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// ADR 033: stored bytes -> one uint16 sample per pixel in bitDepth units.
// Qt-free, never throws.

namespace mviewer::core::rawplain
{

// 'data' points at the first pixel byte (headerOffset already skipped) and
// holds 'size' bytes. Needs size >= effectiveRowStride*(height-1) +
// minRowBytes. out is resized to width*height, row-major. Values are masked
// to bitDepth bits (Lsb16: values above bitDepth are clamped, never wrapped;
// Msb16: value >> (16 - bitDepth)).
// byteOrder applies to Lsb16/Msb16 only. MIPI RAW10: bytes b0..b3 hold the
// 8 MSBs of samples 0..3, b4 holds their 2 LSBs (sample k in bits 2k..2k+1).
// MIPI RAW12: b0, b1 hold the 8 MSBs of samples 0, 1; b2 low nibble = LSBs of
// sample 0, high nibble = LSBs of sample 1. A partial last group (width not a
// multiple of 4 / 2) is still read from its full group bytes. Bits1: 8 samples
// per byte, MSB-first (sample 0 is bit 7); each row is padded to a whole byte;
// 1 is white. False (out cleared) when params are invalid or data is too short.
bool unpackPlainRaw(const Params &p, const std::uint8_t *data, std::size_t size,
                    std::vector<std::uint16_t> &out);

// One sample at (x, y) from the same bytes; -1 when out of range or too short.
int sampleAt(const Params &p, const std::uint8_t *data, std::size_t size, int x, int y);

// Largest stored container value's bit width for Packing::Lsb16 when data
// passes the same size check as unpackPlainRaw (0 when every sample is 0).
// Any other packing, a null pointer or a short buffer returns p.bitDepth.
// Never throws.
int lsb16SignificantBits(const Params &p, const std::uint8_t *data, std::size_t size);

} // namespace mviewer::core::rawplain
