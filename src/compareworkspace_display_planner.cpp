#include "compareworkspace_display_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mviewer::ui
{
namespace
{
constexpr double kDisplayLodOverscan = 1.25;
constexpr int kMaxCompareLodEdge = 4096;

int clampEdge(double value)
{
    if (!std::isfinite(value))
        return 64;
    return std::clamp(static_cast<int>(std::ceil(value)), 64, kMaxCompareLodEdge);
}

CompareDisplayRect fullRect(const CompareDisplayPlanningInput &input)
{
    return {0, 0, input.sourceWidth, input.sourceHeight};
}

double independentFit(const CompareDisplayPlanningInput &input)
{
    if (input.viewportWidth <= 0 || input.viewportHeight <= 0)
        return 0.0;
    return std::min(static_cast<double>(input.viewportWidth) / input.sourceWidth,
                    static_cast<double>(input.viewportHeight) / input.sourceHeight);
}

double uniformFit(const std::vector<double> &fits)
{
    double common = std::numeric_limits<double>::infinity();
    for (const double fit : fits)
    {
        if (fit > 0.0 && std::isfinite(fit))
            common = std::min(common, fit);
    }
    return std::isfinite(common) ? common : 0.0;
}

} // namespace

CompareDisplayPlan planCompareDisplay(const CompareDisplayPlanningInput &input)
{
    CompareDisplayPlan plan;
    if (input.sourceWidth <= 0 || input.sourceHeight <= 0)
        return plan;

    const CompareDisplayRect full = fullRect(input);
    const double dpr = std::max(1.0, input.devicePixelRatio);
    const double paneScale = std::max(1.0, input.paneScale);
    const int baseEdge = clampEdge(std::max(input.viewportWidth, input.viewportHeight) * dpr *
                                   paneScale * kDisplayLodOverscan);
    plan.targetWidth = baseEdge;
    plan.targetHeight = baseEdge;
    plan.sourceRect = full;

    // A placeholder has authoritative dimensions but no widget transform yet.
    // Always request a full-frame LOD until the first raster establishes it.
    if (!input.hasWidgetSourceSize || input.hasCropOrRotation)
        return plan;

    double fitScale = 0.0;
    if (!input.uniformScale)
        fitScale = independentFit(input);
    else
        fitScale = uniformFit(input.fitScales);

    const double zoomRatio = fitScale > 0.0 && std::isfinite(fitScale) &&
                                     input.currentScale > 0.0 &&
                                     std::isfinite(input.currentScale)
                                 ? input.currentScale / fitScale
                                 : 1.0;
    if (!(zoomRatio > 1.0 + 1e-6) || !input.visibleSourceRect.isValid())
        return plan;

    const CompareDisplayRect visible = input.visibleSourceRect;
    const int marginX = std::max(16, visible.width / 8);
    const int marginY = std::max(16, visible.height / 8);
    const int left = std::max(0, visible.x - marginX);
    const int top = std::max(0, visible.y - marginY);
    const int right = std::min(input.sourceWidth, visible.x + visible.width + marginX);
    const int bottom = std::min(input.sourceHeight, visible.y + visible.height + marginY);
    const CompareDisplayRect covered{left, top, right - left, bottom - top};
    if (!covered.isValid())
        return plan;

    const long long fullPixels = static_cast<long long>(full.width) * full.height;
    const long long coveredPixels = static_cast<long long>(covered.width) * covered.height;
    if (fullPixels > 0 && coveredPixels * 100 >= fullPixels * 90)
    {
        plan.targetWidth = clampEdge(std::max(input.viewportWidth, input.viewportHeight) * dpr *
                                     zoomRatio * kDisplayLodOverscan);
        plan.targetHeight = plan.targetWidth;
        return plan;
    }

    plan.sourceRect = covered;
    const double density = std::max(1.0, zoomRatio) * dpr * kDisplayLodOverscan;
    plan.targetWidth = clampEdge(covered.width * density);
    plan.targetHeight = clampEdge(covered.height * density);
    plan.region = true;
    return plan;
}

} // namespace mviewer::ui
