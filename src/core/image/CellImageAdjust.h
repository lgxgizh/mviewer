#pragma once

#include "core/image/ImageBuffer.h"

#include <functional>

namespace mviewer::core
{

// Full-resolution Compare cell adjustment. Geometry matches
// applyCompareAdjustments (crop, then flip, then rotation). USM runs after the
// tonal LUT and before geometry, and only when usmEnabled is set.
struct CellImageAdjust
{
    int brightness = 0;
    float contrast = 1.0f;
    float gamma = 1.0f;
    float rGain = 1.0f;
    float bGain = 1.0f;
    int rotation = 0;
    bool flipH = false;
    bool flipV = false;
    bool hasCrop = false;
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;
    bool usmEnabled = false;
    double usmAmount = 1.5; // [0, 5], UI 0%–500%
    double usmRadius = 1.5; // [0.1, 20] px
    int usmThreshold = 4;   // [0, 255]
    bool usmLumaOnly = false;

    // True when USM is enabled and UsmParams would change pixels.
    bool usmActive() const noexcept;
    bool isIdentity() const noexcept;
};

// Apply tonal LUT, optional USM (applyUsmSharpen), then crop / flip / rotation.
// `cancelled` aborts between stages and inside USM. Identity returns `src`
// unchanged. The source buffer is never mutated.
ImageData applyCellImageAdjust(const ImageData &src, const CellImageAdjust &adjust,
                               const std::function<bool()> &cancelled = {});

} // namespace mviewer::core
