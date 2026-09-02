#pragma once
// M26 Phase 5: full-image statistics computed off the UI thread. The preview
// panel's luminance/RGB means used to iterate every pixel of the full image on
// the GUI thread inside the load callback — a multi-second stall for 24 MP
// files. This helper is pure std over ImageData so it can run on a scheduler
// worker; the UI thread only receives the small result struct.

#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"

#include <cstdint>
#include <functional>

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

// Source-domain RGB statistics for a half-open ROI. Ratios are derived from
// channel means (equivalently channel sums), never from per-pixel ratios.
// ratiosValid is false when mean G is zero, allowing the UI to display an
// explicit unavailable value instead of NaN/Inf.
struct ROIChannelStats
{
    double rMean = 0.0;
    double gMean = 0.0;
    double bMean = 0.0;
    double rOverG = 0.0;
    double bOverG = 0.0;
    int64_t pixelCount = 0;
    bool valid = false;
    bool ratiosValid = false;
    bool cancelled = false;
};

// Iterates the full image once and computes mean luminance + per-channel RGB
// means. Handles RGB24/RGBA32/BGR24/BGRA32/Grayscale8. Returns
// PreviewStats{valid=false} for null input.
PreviewStats computePreviewStats(const ImageData &img);

// Region-aware variant used by interactive ROI selection. It scans the
// clipped source stride directly and never allocates a cropped ImageData.
PreviewStats computePreviewStatsROI(const ImageData &img, const mviewer::domain::Selection &region);

// Authoritative format-aware source/analysis RGB measurement. This is pure
// core code and handles RGB/BGR, alpha, and grayscale storage without a Qt
// presentation conversion.
ROIChannelStats computeROIChannelStats(const ImageData &img,
                                       const mviewer::domain::Selection &region);
ROIChannelStats computeROIChannelStats(const ImageData &img,
                                       const mviewer::domain::Selection &region,
                                       const std::function<bool()> &isCancelled);

} // namespace mviewer::core
