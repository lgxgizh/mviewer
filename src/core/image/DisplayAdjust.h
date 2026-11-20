#pragma once

// Review display adjustment (report feature 5): exposure, gamma, auto-stretch
// and an optional 3D LUT, applied to the display materialization.
//
// The adjustment is presentation-only — it never touches the source pixels —
// and it travels inside DisplayColorContext, which is already carried by every
// asynchronous display/tile request. That keeps the raster caches correct: the
// adjustment fingerprint is part of the context cache key.

#include "core/image/ImageBuffer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::core
{

struct DisplayAdjust
{
    // Stops of exposure, applied in linear-ish display space.
    double exposureEv = 0.0;
    // Display gamma exponent (1.0 = off).
    double gamma = 1.0;
    // Per-channel min/max stretch derived from the source histogram.
    bool autoStretch = false;

    // Optional 3D LUT: lutSize^3 RGB triples, blue-major (Adobe .cube order).
    int lutSize = 0;
    std::vector<std::uint8_t> lut;

    bool identity() const;
    // Stable 64-bit fingerprint for cache keys.
    uint64_t fingerprint() const;

    /// Parse an Adobe .cube file. Returns false and fills `error` otherwise.
    static bool loadCube(const std::string &path, DisplayAdjust &out, std::string &error);
    /// True when `path` looks like a .cube file.
    static bool isCubePath(const std::string &path);
};

/// Apply the adjustment. Returns `src` unchanged when the adjustment is an
/// identity, so the caller keeps the cheap shared-buffer path.
ImageData applyDisplayAdjust(const ImageData &src, const DisplayAdjust &adjust);

} // namespace mviewer::core
