#pragma once
#include <string>
#include <cstdint>

namespace mviewer::domain {

// Immutable pixel coordinate
struct PixelCoord {
    int x = 0, y = 0;
    bool operator==(const PixelCoord& o) const = default;
};

// Pixel color value (RGBA)
struct PixelColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const PixelColor& o) const = default;
};

// Image metadata (file-level)
struct ImageMetadata {
    std::string filePath;
    std::string fileName;
    int width = 0;
    int height = 0;
    int64_t fileSize = 0;
    int64_t modifiedEpochSec = 0;
    std::string hash;
};

// Domain-level image identifier (value type)
struct ImageId {
    std::string hash;
    bool operator==(const ImageId& o) const = default;
    bool empty() const { return hash.empty(); }
};

} // namespace mviewer::domain
