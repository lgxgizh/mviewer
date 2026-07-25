// MetadataReader — extracts file-level metadata (size, mtime, dimensions, hash
// key) for an image path. Extracted from ImageRepository so the Repository stays
// a thin orchestrator (Review P0-1: Repository -> Manager delegation).
//
// Qt types are permitted here (core/ layer); this is NOT the domain/ layer.
#pragma once

#include <string>

#include "domain/Image.h"

namespace mviewer::core
{

class MetadataReader
{
  public:
    // Stable cache key: absolute path + size + mtime. Two loads of the same
    // file at the same size/mtime resolve to the same cache entry.
    static std::string key(const std::string &filePath);

    // File-level metadata: path, name, size, mtime, pixel dimensions. Does NOT
    // decode pixels; dimension is read cheaply via QImageReader::size().
    static mviewer::domain::ImageMetadata read(const std::string &filePath);

  private:
    // P0: parse GPS IFD from a JPEG file buffer. Populates hasGps, gpsLatitude,
    // gpsLongitude, gpsAltitude on the metadata struct in-place.
    static void readGps(mviewer::domain::ImageMetadata &meta, const std::string &filePath);
    // Helper: convert EXIF rational (num, denom) to double degrees.
    static double exifToDecimal(const unsigned char *buf, int offset, bool isLittle);
};

} // namespace mviewer::core
