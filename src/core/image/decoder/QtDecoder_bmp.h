#pragma once

#include <QImage>
#include <QSize>
#include <string>

namespace mviewer::core::detail
{

// True when `path` is a BMP this reader handles: "BM" magic, DIB header size
// 40/52/56/108/124, planes 1, bpp 8 (palette), 24 or 32, compression BI_RGB(0),
// or BI_BITFIELDS(3) only for 32 bpp with masks R=0x00FF0000 G=0x0000FF00
// B=0x000000FF and alpha mask 0 (or absent), width 1..65535, |height| 1..65535,
// pixel data offset + stride*|height| <= file size. On true, *size gets
// (width, |height|) if size != nullptr.
bool bmpBoxLodEligible(const std::string &path, QSize *size);

// Streams rows and box-averages into a target-size QImage::Format_RGB888 image.
// Never allocates the full-size raster: memory is one source row + one uint64
// accumulator row (3 channels * target width). Returns a null QImage when not
// eligible, on any read error, or if target is invalid/larger than the source.
QImage decodeBmpBoxLod(const std::string &path, const QSize &target);

} // namespace mviewer::core::detail
