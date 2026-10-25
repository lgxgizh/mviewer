#pragma once

// JPEG APP1 / IFD0 Orientation helpers. Qt-free: callers in core pass bytes.
// Orientation values follow the EXIF 1..8 D4 group (see ExifOrientation.h).

#include <cstdint>
#include <optional>
#include <vector>

namespace mviewer::core
{

// Pixel operation applied on top of the current EXIF orientation. Defined
// here so composeOrientation stays in a public, Qt-free header.
enum class FilePixelOp : std::uint8_t
{
    Rotate = 0,
    FlipH,
    FlipV
};

// IFD0 tag 0x0112 when it is a SHORT of count 1 in range 1..8. Missing,
// truncated, or out-of-range input returns nullopt.
std::optional<int> readJpegOrientation(const std::vector<uint8_t> &jpeg);

// Rewrite an existing IFD0 0x0112 tag in place. Does not add a tag.
// `orientation` must be 1..8. II and MM are both accepted. Any out-of-bounds
// or missing-tag condition leaves `jpeg` unchanged and returns false.
bool writeJpegOrientationInPlace(std::vector<uint8_t> &jpeg, int orientation);

// Full APP1 Exif segment, including the FF E1 marker and the length field.
// Empty when the file has no Exif APP1.
std::vector<uint8_t> extractJpegApp1Exif(const std::vector<uint8_t> &jpeg);

// Insert `app1` (a full FF E1 segment) after SOI, or after a leading JFIF
// APP0 when that is the first segment. Existing Exif APP1 segments in
// `encoded` are removed so the result has at most one. Non-Exif APP1
// (for example XMP) is kept. When `resetOrientationTo1` is set, an existing
// IFD0 orientation tag inside `app1` is forced to 1; a missing tag is not
// created (viewers treat that as orientation 1).
std::vector<uint8_t> spliceJpegApp1Exif(const std::vector<uint8_t> &encoded,
                                        const std::vector<uint8_t> &app1,
                                        bool resetOrientationTo1);

// Compose `op` onto EXIF orientation `current` (1..8). Values outside 1..8
// are treated as 1. Rotate degrees are normalized modulo 360; a non-multiple
// of 90 returns `current` (after the 1..8 normalization).
int composeOrientation(int current, FilePixelOp op, int degreesCw);

// SOF dimensions before the orientation tag is applied. False when no SOF
// marker is present or the header is truncated.
bool readJpegStoredSize(const std::vector<uint8_t> &jpeg, int &width, int &height);

} // namespace mviewer::core
