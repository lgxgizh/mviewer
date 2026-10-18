#include "core/image/CellImageAdjust.h"

#include "core/analysis/ExportReport.h"
#include "core/image/ImageAdjust.h"
#include "core/image/UsmSharpen.h"

#include <cmath>

namespace mviewer::core
{

bool CellImageAdjust::usmActive() const noexcept
{
    if (!usmEnabled)
        return false;
    UsmParams params;
    params.amount = usmAmount;
    params.radius = usmRadius;
    params.threshold = usmThreshold;
    params.lumaOnly = usmLumaOnly;
    return !params.isIdentity();
}

bool CellImageAdjust::isIdentity() const noexcept
{
    return brightness == 0 && std::abs(contrast - 1.0f) < 1e-6f && std::abs(gamma - 1.0f) < 1e-6f &&
           std::abs(rGain - 1.0f) < 1e-6f && std::abs(bGain - 1.0f) < 1e-6f && rotation == 0 &&
           !flipH && !flipV && !hasCrop && !usmActive();
}

namespace
{

bool stopRequested(const std::function<bool()> &cancelled)
{
    return cancelled && cancelled();
}

} // namespace

ImageData applyCellImageAdjust(const ImageData &src, const CellImageAdjust &adjust,
                               const std::function<bool()> &cancelled)
{
    if (stopRequested(cancelled))
        return {};
    if (src.isNull() || adjust.isIdentity())
        return src;

    ImageData cur = src;
    const bool color = adjust.brightness != 0 || std::abs(adjust.contrast - 1.0f) >= 1e-6f ||
                       std::abs(adjust.gamma - 1.0f) >= 1e-6f ||
                       std::abs(adjust.rGain - 1.0f) >= 1e-6f ||
                       std::abs(adjust.bGain - 1.0f) >= 1e-6f;
    if (color)
    {
        cur = adjustCombinedLut(cur, adjust.brightness, adjust.contrast, adjust.gamma, adjust.rGain,
                                adjust.bGain);
        if (stopRequested(cancelled) || cur.isNull())
            return {};
    }

    if (adjust.usmActive())
    {
        UsmParams params;
        params.amount = adjust.usmAmount;
        params.radius = adjust.usmRadius;
        params.threshold = adjust.usmThreshold;
        params.lumaOnly = adjust.usmLumaOnly;
        cur = applyUsmSharpen(cur, params, cancelled);
        if (stopRequested(cancelled) || cur.isNull())
            return {};
    }

    CompareAdjustmentState geometry;
    geometry.rotation = adjust.rotation;
    geometry.flipH = adjust.flipH;
    geometry.flipV = adjust.flipV;
    geometry.hasCrop = adjust.hasCrop;
    geometry.cropX = adjust.cropX;
    geometry.cropY = adjust.cropY;
    geometry.cropW = adjust.cropW;
    geometry.cropH = adjust.cropH;
    return applyCompareAdjustments(cur, geometry, cancelled);
}

} // namespace mviewer::core
