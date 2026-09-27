#pragma once

#include "core/image/ImageBuffer.h"

#include <vector>

// In-memory power-of-two mipmap chain helpers (Qt-free).
//
// LOD convention matches TileCache: lod 0 = full / finest raster; each step
// up halves both dimensions (box average), so higher lod = coarser.
// buildMipChain returns levels in fine→coarse order: [0]=full, [1]=½, …
namespace mviewer::cache
{

// Default stop edge: keep generating until max(w,h) <= minEdge.
inline constexpr int kDefaultMipMinEdge = 256;

// Deterministic 2×2 (or partial-edge) box-average half-scale. Preserves
// PixelFormat / channel count. Returns null on invalid input; if either
// dimension is already 1, that axis stays 1.
ImageData downscaleHalfBox(const ImageData &src);

// Build the mip chain from a full raster. Level 0 aliases `full` (shared
// buffer — cheap). Subsequent levels are freshly allocated. Stops when
// max(w,h) <= minEdge, or when a step would not shrink further.
// Empty input → empty vector.
std::vector<ImageData> buildMipChain(const ImageData &full, int minEdge = kDefaultMipMinEdge);

// max(w,h) helper.
inline int imageMaxEdge(const ImageData &img) noexcept
{
    return img.isNull() ? 0 : (img.width > img.height ? img.width : img.height);
}

} // namespace mviewer::cache
