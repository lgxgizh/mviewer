#pragma once

#include "core/image/ImageBuffer.h"

#include <string>
#include <vector>

namespace mviewer::core
{

// Where a text watermark is placed on the image.
enum class WatermarkPosition
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center,
    Tile
};

// Resize keeping aspect ratio so the result fits within maxW x maxH.
// A non-positive dimension means "no constraint" on that axis.
// Images smaller than the constraint are NOT upscaled.
ImageData resizeToFit(const ImageData &src, int maxW, int maxH);

// Scale by factor (>0). factor = 0.5 halves both dimensions.
ImageData resizeByFactor(const ImageData &src, double factor);

// Draw a text watermark. An empty `text` returns a copy unchanged.
// `opacity01` is clamped to [0, 1]. `fontSizePx` is the font pixel size.
ImageData addTextWatermark(const ImageData &src, const std::string &text,
                           WatermarkPosition pos, double opacity01, int fontSizePx);

// Compose a contact sheet: `cols` columns, each thumbnail `thumb` px on its
// long edge, images laid out row by row. Returns an empty image if `imgs` is empty.
ImageData makeContactSheet(const std::vector<ImageData> &imgs, int cols, int thumb);

// Apply a rename pattern to a base file name. Supported tokens:
//   {name}  original base name (without extension)
//   {ext}   original extension (without dot)
//   {n}     1-based index
//   {total} total count
//   {seq:W} 1-based index zero-padded to width W (e.g. {seq:3} -> 001)
// Returns the new base name (no extension).
std::string applyRenamePattern(const std::string &pattern, const std::string &baseName,
                               const std::string &ext, int index, int total);

// Write a multi-page PDF, embedding each image as a JPEG stream (one image per
// page, page sized to the image). Returns true on success.
bool writePdf(const std::string &path, const std::vector<ImageData> &images, int quality);

} // namespace mviewer::core
