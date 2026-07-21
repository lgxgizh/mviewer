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
    std::string lensMaker;      // lens manufacturer
    std::string serialNumber;   // body serial number

    // Exposure
    uint32_t iso = 0;           // ISO sensitivity (e.g. 100, 6400)
    double exposureSec = 0.0;   // shutter speed in seconds (e.g. 0.004 = 1/250)
    double fNumber = 0.0;       // aperture (e.g. 2.8, 5.6)
    double focalLength = 0.0;   // focal length in mm (e.g. 85.0)
    double focalLength35mm = 0.0; // 35mm equivalent focal length
    std::string exposureProgram; // "Manual", "Aperture Priority", etc.
    std::string meteringMode;   // "Multi-segment", "Center-weighted", "Spot"
    double exposureCompensation = 0.0; // EV compensation
    uint32_t isoSpeed = 0;      // ISO speed rating
    std::string flash;          // "Did not fire", "Fired", etc.
    double shutterSpeedValue = 0.0; // APEX shutter speed value
    double apertureValue = 0.0; // APEX aperture value
    double brightnessValue = 0.0; // APEX brightness

    // Sensor
    std::string bayerPattern;   // "RGGB", "BGGR", "GRBG", "GBRG"
    uint16_t blackLevel = 0;    // per-channel black level (16-bit)
    uint16_t whiteLevel = 0;    // per-channel white level (16-bit)
    double colorMatrix[12] = {}; // 3x4 camera color matrix (DNG/EXIF)
    uint32_t colorMatrixCount = 0; // number of valid entries (up to 12)
    double whiteBalanceRGGB[4] = {}; // per-channel WB gains (R, G1, G2, B)
    uint32_t whiteBalanceCount = 0;
    uint32_t width = 0;         // sensor active width
    uint32_t height = 0;        // sensor active height
    uint32_t bitsPerSample = 0; // bit depth per channel
    std::string sensorType;     // "Color Filter Array", "Monochrome"
    uint32_t opticalBlack[4] = {}; // per-quad optical black level
    uint32_t opticalBlackCount = 0;

    // Shot
    std::string whiteBalance;   // "Auto", "Daylight", "Manual"
    std::string shutterType;    // "Mechanical", "Electronic"
    std::string colorSpace;     // "sRGB", "Adobe RGB", "ProPhoto"
    std::string dateTime;       // capture timestamp
    std::string dateTimeOriginal; // original capture time
    std::string description;    // image description / user comment

    // Calibration
    std::string cameraCalibration[4] = {}; // camera calibration signatures
    uint32_t cameraCalibrationCount = 0;
    std::string noiseProfile;   // noise profile (DNG)
    double noiseReduction = 0.0; // applied noise reduction
    std::string makerNote;      // manufacturer-specific note (hex summary)

    bool parsed = false;        // true if any RAW tag was found
};

// Parse RAW metadata from a file path. Returns parsed=false if the file is
// not a recognized RAW format or no RAW tags could be extracted.
RawMetadata parseRawMetadata(const std::string &filePath);

} // namespace mviewer::core
