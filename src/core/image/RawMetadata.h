#pragma once

#include <cstdint>
#include <string>

namespace mviewer::core
{

// RAW-specific sensor metadata, extracted from the TIFF/EXIF structure of
// RAW formats (DNG/ARW/NEF/ORF/RW2 are TIFF-based). CR3 is ISO-BBMF and
// parsed best-effort. Self-contained: no external RAW library (libraw /
// RawSpeed) — license + build-complexity risk avoided per M14 RFC Phase A.
struct RawMetadata
{
    // Camera
    std::string make;           // "SONY", "Canon", "NIKON", "FUJIFILM"
    std::string model;          // "ILCE-7RM4", "EOS R5"
    std::string lens;           // lens model if present

    // Exposure
    uint32_t iso = 0;           // ISO sensitivity (e.g. 100, 6400)
    double exposureSec = 0.0;   // shutter speed in seconds (e.g. 0.004 = 1/250)
    double fNumber = 0.0;       // aperture (e.g. 2.8, 5.6)
    double focalLength = 0.0;   // focal length in mm (e.g. 85.0)

    // Sensor
    std::string bayerPattern;   // "RGGB", "BGGR", "GRBG", "GBRG"
    uint16_t blackLevel = 0;    // per-channel black level (16-bit)
    uint16_t whiteLevel = 0;    // per-channel white level (16-bit)
    uint32_t isoSpeed = 0;      // ISO speed rating

    // Shot
    std::string whiteBalance;   // "Auto", "Daylight", "Manual"
    std::string shutterType;    // "Mechanical", "Electronic"

    bool parsed = false;        // true if any RAW tag was found
};

// Parse RAW metadata from a file path. Returns parsed=false if the file is
// not a recognized RAW format or no RAW tags could be extracted.
RawMetadata parseRawMetadata(const std::string &filePath);

} // namespace mviewer::core
