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
    static std::string key(const std::string& filePath);

    // File-level metadata: path, name, size, mtime, pixel dimensions. Does NOT
    // decode pixels; dimension is read cheaply via QImageReader::size().
    static mviewer::domain::ImageMetadata read(const std::string& filePath);
};

} // namespace mviewer::core
