#include "compareworkspace_display_pyramid.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using mviewer::ui::CompareDisplayPlan;
using mviewer::ui::CompareDisplayPlanningInput;

static CompareDisplayPlanningInput makeInput(int sw, int sh, int vw, int vh, double scale)
{
    CompareDisplayPlanningInput in;
    in.sourceWidth = sw;
    in.sourceHeight = sh;
    in.viewportWidth = vw;
    in.viewportHeight = vh;
    in.currentScale = scale;
    in.paneScale = scale;
    in.devicePixelRatio = 1.0;
    in.hasWidgetSourceSize = true;
    in.visibleSourceRect = {0, 0, sw, sh};
    return in;
}

int main()
{
    int failures = 0;

    auto cold = makeInput(12000, 8333, 1200, 800, 0.1);
    cold.hasWidgetSourceSize = false;
    const auto pyramid = mviewer::ui::planCompareDisplayPyramid(cold);
    const bool sizeOk = pyramid.size() >= 2 && pyramid.size() <= 3;
    std::printf("%s: pyramid has 2-3 levels (%zu)\n", sizeOk ? "PASS" : "FAIL", pyramid.size());
    if (!sizeOk)
        ++failures;

    bool ordered = true;
    for (size_t i = 1; i < pyramid.size(); ++i)
    {
        const int prev = std::max(pyramid[i - 1].targetWidth, pyramid[i - 1].targetHeight);
        const int cur = std::max(pyramid[i].targetWidth, pyramid[i].targetHeight);
        if (cur < prev)
            ordered = false;
    }
    std::printf("%s: pyramid edges non-decreasing\n", ordered ? "PASS" : "FAIL");
    if (!ordered)
        ++failures;

    std::vector<CompareDisplayPlan> have = pyramid;
    // Desire finest; coarsest covering should be selected first if it covers.
    const CompareDisplayPlan desired = pyramid.back();
    const int ready = mviewer::ui::selectReadyPyramidLevel(have, desired);
    const bool readyOk = ready >= 0 && ready < static_cast<int>(have.size());
    std::printf("%s: selectReadyPyramidLevel returns valid index (%d)\n", readyOk ? "PASS" : "FAIL",
                ready);
    if (!readyOk)
        ++failures;

    const int finer = mviewer::ui::nextFinerPyramidLevel(pyramid, 0);
    const bool finerOk = pyramid.size() == 1 ? finer < 0 : finer == 1;
    std::printf("%s: nextFinerPyramidLevel from 0\n", finerOk ? "PASS" : "FAIL");
    if (!finerOk)
        ++failures;

    const bool accept = mviewer::ui::shouldAcceptDisplayDelivery(3, 3, false, false);
    const bool rejectStale = !mviewer::ui::shouldAcceptDisplayDelivery(2, 3, false, false);
    const bool rejectProv =
        !mviewer::ui::shouldAcceptDisplayDelivery(3, 3, true, /*alreadyHaveFull=*/true);
    std::printf("%s: accept current gen\n", accept ? "PASS" : "FAIL");
    std::printf("%s: reject stale gen\n", rejectStale ? "PASS" : "FAIL");
    std::printf("%s: reject provisional over full\n", rejectProv ? "PASS" : "FAIL");
    if (!accept)
        ++failures;
    if (!rejectStale)
        ++failures;
    if (!rejectProv)
        ++failures;

    const bool decodeVisible = mviewer::ui::preferDecodePriorityForDisplay(false, true, false) &&
                               mviewer::ui::preferDecodePriorityForDisplay(false, false, true) &&
                               !mviewer::ui::preferDecodePriorityForDisplay(false, false, false);
    std::printf("%s: preferDecodePriorityForDisplay\n", decodeVisible ? "PASS" : "FAIL");
    if (!decodeVisible)
        ++failures;

    std::printf("=== Compare display pyramid tests: %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
