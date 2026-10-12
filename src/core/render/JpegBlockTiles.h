#pragma once

#include "core/image/ImageBuffer.h"

#include <functional>
#include <string>

namespace mviewer::core
{

// Zoomed-in JPEG tiles: libjpeg cannot seek, so every clipped region decode
// re-reads the file from the first scanline. Decoding each 256 px tile on its
// own therefore repeats that scan per tile (12 tiles at the bottom of a 100 MP
// JPEG: ~3.6 s). Instead decode one aligned kJpegBlockEdge block once, keep a
// few recent blocks, and slice tiles out of it. Pixels are identical: a 1:1
// clip decode is an exact crop of the scanlines either way.
inline constexpr int kJpegBlockEdge = 1024;

// Decodes the block whose top-left corner is (x, y), edge `edge`, clamped to
// the image. Returns null when the source has no bounded/native region decode.
using JpegBlockDecoder = std::function<ImageData(int x, int y, int edge)>;

// True for a 1:1 tile (no scaling) of a .jpg/.jpeg file that lies
// within one block.
bool jpegBlockEligible(const std::string &path, int srcX, int srcY, int srcW, int srcH, int targetW,
                       int targetH);

// Tile cut from the cached (or freshly decoded) block; null on failure so the
// caller can fall back to its regular per-tile decode.
ImageData jpegBlockTile(const std::string &path, int srcX, int srcY, int srcW, int srcH,
                        const JpegBlockDecoder &decodeBlock);

// Test hooks.
void clearJpegBlockCache();
int jpegBlockDecodeCount();

} // namespace mviewer::core
