#pragma once
#include <cstdint>
#include <map>
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

    // M-XX: parsed ICC profile details (from the embedded profile bytes).
    std::string iccDescription;     // profile description, e.g. "sRGB IEC61966-2.1"
    std::string iccCopyright;       // profile copyright notice
    std::string iccColorSpace;      // profile data color space, e.g. "RGB"
    std::string iccDeviceClass;     // profile device class, e.g. "显示器"
    std::string iccPcs;             // profile connection space, e.g. "XYZ"
    std::string iccRenderingIntent; // e.g. "感知 (Perceptual)"
    std::string iccVersion;         // profile version, e.g. "2.1.0"
    std::string format;             // container format, e.g. "JPEG","PNG","BMP","TIFF"
    int dpiX = 0, dpiY = 0;         // physical resolution in DPI (0 = unknown)

    // M18: embedded text metadata (EXIF/XMP/IPTCCore keys the plugin exposes),
    // e.g. "Make", "Model", "DateTimeOriginal", "Software". Best-effort; the
    // exact keys available depend on the container + Qt image plugin.
    std::map<std::string, std::string> textKeys;

    // ─── P0: GPS metadata (EXIF GPS IFD) ──────────────────────────────────────
    bool hasGps = false;
    double gpsLatitude = 0.0;  // decimal degrees, positive = North
    double gpsLongitude = 0.0; // decimal degrees, positive = East
    double gpsAltitude = 0.0;  // meters above sea level (0 = unknown)

    // ─── Session persistence (M12.1) ────────────────────────────────────────
    // ROI (in image pixel coords) captured for this image during a Compare
    // session, and the last analysis result text plus its producer identity.
    // Round-tripped by WorkspaceSerializer so a saved .mvws restores
    // compare/analysis context without relabeling a result with the current
    // analyzer selection. Zero ROI = no selection saved; empty analysis =
    // none saved. The analyzer id is optional for legacy workspace files.
    int roiX = 0, roiY = 0, roiW = 0, roiH = 0;
    std::string analysis;
    std::string analysisAnalyzerId;
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
