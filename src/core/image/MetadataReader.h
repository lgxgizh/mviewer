// MetadataReader — extracts file-level metadata (size, mtime, dimensions, hash
// key) for an image path. Extracted from ImageRepository so the Repository stays
// a thin orchestrator (Review P0-1: Repository -> Manager delegation).
//
// Qt types are permitted here (core/ layer); this is NOT the domain/ layer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain/Image.h"

namespace mviewer::core
{

namespace detail
{
// Helper: convert EXIF rational (num, denom) to double degrees.
inline double exifToDecimal(const unsigned char *buf, int offset, bool isLittle)
{
    if (!buf || offset < 0)
        return 0.0;
    auto read32 = [](const unsigned char *b, bool little) -> uint32_t
    {
        if (little)
            return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                   (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
               (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
    };
    const uint32_t num = read32(buf + offset, isLittle);
    const uint32_t den = read32(buf + offset + 4, isLittle);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}
} // namespace detail

class MetadataReader
{
  public:
    // Stable cache key: absolute path + size + mtime. Two loads of the same
    // file at the same size/mtime resolve to the same cache entry.
    static std::string key(const std::string &filePath);

    // File-level metadata: path, name, size, mtime, pixel dimensions. Does NOT
    // decode pixels; dimension is read cheaply via QImageReader::size().
    static mviewer::domain::ImageMetadata read(const std::string &filePath);

    // Extracts embedded JPEG thumbnail from EXIF IFD1 if present.
    // Returns raw JPEG bytes, or empty vector if none.
    static std::vector<uint8_t> extractExifThumbnail(const std::string &filePath);

  private:
    // P0: parse GPS IFD from a JPEG file buffer. Populates hasGps, gpsLatitude,
    // gpsLongitude, gpsAltitude on the metadata struct in-place.
    static void readGps(mviewer::domain::ImageMetadata &meta, const std::string &filePath);
};

} // namespace mviewer::core
