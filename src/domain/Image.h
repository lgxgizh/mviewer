#pragma once
#include <cstdint>
#include <string>

namespace mviewer::domain
{

// Immutable pixel coordinate
struct PixelCoord
{
    int x = 0, y = 0;
    bool operator==(const PixelCoord &o) const = default;
};

// Pixel color value (RGBA)
struct PixelColor
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const PixelColor &o) const = default;
};

// Image metadata (file-level)
struct ImageMetadata
{
    std::string filePath;
    std::string fileName;
    int width = 0;
    int height = 0;
    int64_t fileSize = 0;
    int64_t modifiedEpochSec = 0;
    std::string hash;

    // ─── Decode-time enrichment (M6) ──────────────────────────────────────────
    int bitDepth = 0;           // bits per channel (e.g. 8, 16)
    int channels = 0;           // number of color channels (e.g. 3 = RGB)
    std::string colorSpace;     // "sRGB", "AdobeRGB", "DisplayP3", or ""/unknown
    int orientation = 1;        // EXIF orientation 1-8 (1 = normal)
    bool hasIccProfile = false; // true if an ICC profile is embedded
    std::string format;         // container format, e.g. "JPEG","PNG","BMP","TIFF"
};

// Domain-level image identifier (value type)
struct ImageId
{
    std::string hash;
    bool operator==(const ImageId &o) const = default;
    bool empty() const
    {
        return hash.empty();
    }
};

} // namespace mviewer::domain
