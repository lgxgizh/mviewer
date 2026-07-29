#pragma once

#include <cstddef>
#include <string>

// Minimal, dependency-free ICC profile reader.
//
// Parses the embedded ICC profile bytes (header + tag directory) to surface the
// human-relevant fields image-algorithm engineers care about for color
// management: description, copyright, data color space, device class, PCS,
// rendering intent and version. No Qt dependency so it can be called from the
// core/ layer (MetadataReader) which is allowed Qt, while the parsed results
// are plain std::string fields stored on the Qt-free domain ImageMetadata.

namespace mviewer::core
{

struct IccProfile
{
    bool valid = false;
    std::string description;     // profile description, e.g. "sRGB IEC61966-2.1"
    std::string copyright;       // profile copyright notice
    std::string colorSpace;      // profile data color space, e.g. "RGB"
    std::string deviceClass;     // profile device class, e.g. "显示器"
    std::string pcs;             // profile connection space, e.g. "XYZ"
    std::string renderingIntent; // e.g. "感知 (Perceptual)"
    std::string version;         // profile version, e.g. "2.1.0"
};

// Parses an ICC profile from raw bytes. Returns valid=false (all fields empty)
// if the buffer is too small or not a recognizable ICC profile.
IccProfile parseIccProfile(const unsigned char *data, size_t size);

} // namespace mviewer::core
