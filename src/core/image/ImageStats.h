#pragma once
// M26 Phase 5: full-image statistics computed off the UI thread. The preview
// panel's luminance/RGB means used to iterate every pixel of the full image on
// the GUI thread inside the load callback — a multi-second stall for 24 MP
// files. This helper is pure std over ImageData so it can run on a scheduler
// worker; the UI thread only receives the small result struct.

#include "core/image/ImageBuffer.h"

#include <cstdint>

namespace mviewer::core
{

// Small immutable result — the only thing the UI thread needs to paint the
// statistics line.
struct PreviewStats
{
    double lumMean = 0.0;
    int rMean = 0;
    int gMean = 0;
    int bMean = 0;
    bool valid = false;
};

// Iterates the full image once and computes mean luminance + per-channel RGB
// means. Handles RGB24/RGBA32/BGR24/BGRA32/Grayscale8. Returns
// PreviewStats{valid=false} for null input.
PreviewStats computePreviewStats(const ImageData &img);

} // namespace mviewer::core
