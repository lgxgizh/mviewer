#include "compareworkspace_display_pyramid.h"

#include <algorithm>
#include <cmath>

namespace mviewer::ui
{
namespace
{
constexpr int kPyramidFloorEdge = 320;
constexpr int kMaxCompareLodEdge = 4096;

int clampEdge(int value)
{
    return std::clamp(value, 64, kMaxCompareLodEdge);
}

int planEdge(const CompareDisplayPlan &plan)
{
    return std::max(plan.targetWidth, plan.targetHeight);
}

CompareDisplayPlan scalePlanEdge(CompareDisplayPlan plan, int edge)
{
    edge = clampEdge(edge);
    plan.targetWidth = edge;
    plan.targetHeight = edge;
    return plan;
}

bool coversDesired(const CompareDisplayPlan &have, const CompareDisplayPlan &desired)
{
    if (!have.isValid() || !desired.isValid())
        return false;
    // Region mismatch cannot stand in for the desired coverage.
    if (have.region != desired.region)
        return false;
    if (desired.region)
    {
        const long long havePixels =
            static_cast<long long>(have.sourceRect.width) * have.sourceRect.height;
        const long long wantPixels =
            static_cast<long long>(desired.sourceRect.width) * desired.sourceRect.height;
        if (wantPixels <= 0)
            return false;
        // Ready level must cover most of the desired source rect area.
        if (havePixels * 100 < wantPixels * 85)
            return false;
        const double haveDensity =
            static_cast<double>(have.targetWidth) / std::max(1, have.sourceRect.width);
        const double wantDensity =
            static_cast<double>(desired.targetWidth) / std::max(1, desired.sourceRect.width);
        return haveDensity + 1e-9 >= wantDensity * 0.85;
    }
    return planEdge(have) + 1 >= static_cast<int>(planEdge(desired) * 0.85);
}
} // namespace

std::vector<CompareDisplayPlan> planCompareDisplayPyramid(const CompareDisplayPlanningInput &input)
{
    std::vector<CompareDisplayPlan> levels;
    const CompareDisplayPlan full = planCompareDisplay(input);
    if (!full.isValid())
        return levels;

    const int fullEdge = planEdge(full);
    const CompareDisplayPlan cheap = planCompareDisplayCheap(input);
    const int floorEdge =
        cheap.isValid() ? std::max(kPyramidFloorEdge, planEdge(cheap)) : kPyramidFloorEdge;

    const int quarter = clampEdge(std::max(floorEdge, fullEdge / 4));
    const int half = clampEdge(std::max(floorEdge, fullEdge / 2));
    const int fine = clampEdge(fullEdge);

    auto pushUnique = [&levels](const CompareDisplayPlan &plan)
    {
        if (!plan.isValid())
            return;
        if (!levels.empty() && planEdge(levels.back()) == planEdge(plan) &&
            levels.back().region == plan.region &&
            levels.back().sourceRect.x == plan.sourceRect.x &&
            levels.back().sourceRect.y == plan.sourceRect.y &&
            levels.back().sourceRect.width == plan.sourceRect.width &&
            levels.back().sourceRect.height == plan.sourceRect.height)
            return;
        levels.push_back(plan);
    };

    // Always keep full-frame coarse rungs when the fine plan is regional so
    // zoom-out / pan can fall back immediately.
    if (full.region)
    {
        CompareDisplayPlan coarse = full;
        coarse.region = false;
        coarse.sourceRect = {0, 0, input.sourceWidth, input.sourceHeight};
        pushUnique(scalePlanEdge(coarse, quarter));
        pushUnique(scalePlanEdge(coarse, half));
    }
    else
    {
        pushUnique(scalePlanEdge(full, quarter));
        pushUnique(scalePlanEdge(full, half));
    }
    pushUnique(scalePlanEdge(full, fine));
    // Preserve region geometry on the finest rung.
    if (!levels.empty())
    {
        levels.back() = full;
        levels.back().targetWidth = fine;
        levels.back().targetHeight = fine;
    }
    return levels;
}

int selectReadyPyramidLevel(const std::vector<CompareDisplayPlan> &have,
                            const CompareDisplayPlan &desired)
{
    if (!desired.isValid() || have.empty())
        return -1;
    int best = -1;
    for (int i = 0; i < static_cast<int>(have.size()); ++i)
    {
        if (!coversDesired(have[static_cast<size_t>(i)], desired))
            continue;
        // Prefer the coarsest covering level for immediate paint.
        best = i;
        break;
    }
    // If none covers, still return the finest available valid level as fallback.
    if (best < 0)
    {
        for (int i = static_cast<int>(have.size()) - 1; i >= 0; --i)
        {
            if (have[static_cast<size_t>(i)].isValid())
                return i;
        }
    }
    return best;
}

int nextFinerPyramidLevel(const std::vector<CompareDisplayPlan> &pyramid, int current)
{
    if (pyramid.empty() || current < 0)
        return pyramid.empty() ? -1 : 0;
    if (current + 1 >= static_cast<int>(pyramid.size()))
        return -1;
    return current + 1;
}

} // namespace mviewer::ui
