#pragma once

// ColorChecker (report feature 6): measure a 24-patch chart against the
// published reference L*a*b* values and report CIEDE2000 per patch.
//
// The chart is located by a caller-supplied rectangle (the viewer's ROI or the
// current selection). Sampling a 4x6 grid inside that rectangle is far more
// robust than hunting for patch edges, and it lets the user correct a slightly
// tilted chart by tightening the rectangle.

#include "core/analysis/Ciede2000.h"
#include "core/image/ImageBuffer.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace mviewer::core
{

// Classic Macbeth ColorChecker, 24 patches, CIE L*a*b* under D65 / 2°.
// Widely republished reference set (BabelColor "average" measurements).
constexpr std::size_t kColorCheckerPatchCount = 24;

struct ColorCheckerPatch
{
    const char *name;
    Lab reference;
};

const std::array<ColorCheckerPatch, kColorCheckerPatchCount> &colorCheckerReference();

struct ColorCheckerSample
{
    Lab measured;
    double deltaE = 0.0;
    bool valid = false;
};

struct ColorCheckerResult
{
    ColorCheckerSample patches[kColorCheckerPatchCount];
    double meanDeltaE = 0.0;
    double maxDeltaE = 0.0;
    int validCount = 0;
};

/// Sample the 24 patches on a 4-row x 6-column grid inside `rect` (image
/// coordinates) and compare them with the reference values. Patches whose
/// sample falls outside the image are reported invalid.
ColorCheckerResult measureColorChecker(const ImageData &image, int rectX, int rectY, int rectW,
                                       int rectH);

/// Human-readable report body (one line per patch plus the summary).
std::string colorCheckerReportText(const ColorCheckerResult &result);

} // namespace mviewer::core
