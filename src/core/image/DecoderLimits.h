#pragma once

#include <cstdint>

// Shared input budget for decoders that parse their own container header.
//
// A hostile file can declare an enormous raster for almost no bytes: PNG has no
// total-pixel limit by default, TIFF allows 65535 repeated strip offsets, and a
// BMP DIB can claim any dimensions. Checking width*height before the decompress
// or I/O amplification starts keeps a small file from turning into tens of GiB
// of work. The number matches PlainRawFormat's sample budget so every decoder
// answers the same question with the same answer.
namespace mviewer::core
{
inline constexpr int kMaxImageDimension = 65536;
inline constexpr int64_t kMaxImagePixels = int64_t{1} << 28; // 268M samples
} // namespace mviewer::core
