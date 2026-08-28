#include "compareworkspace_display_planner.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{

using mviewer::ui::CompareDisplayPlan;
using mviewer::ui::CompareDisplayPlanningInput;
using mviewer::ui::CompareDisplayRect;

CompareDisplayPlanningInput input(int sourceWidth, int sourceHeight, int viewportWidth,
                                  int viewportHeight, double currentScale, double dpr = 1.0)
{
    CompareDisplayPlanningInput result;
    result.sourceWidth = sourceWidth;
    result.sourceHeight = sourceHeight;
    result.viewportWidth = viewportWidth;
    result.viewportHeight = viewportHeight;
    result.currentScale = currentScale;
    result.paneScale = currentScale;
    result.devicePixelRatio = dpr;
    result.hasWidgetSourceSize = true;
    return result;
}

bool samePlan(const CompareDisplayPlan &a, const CompareDisplayPlan &b)
{
    return a.targetWidth == b.targetWidth && a.targetHeight == b.targetHeight &&
           a.sourceRect.x == b.sourceRect.x && a.sourceRect.y == b.sourceRect.y &&
           a.sourceRect.width == b.sourceRect.width && a.sourceRect.height == b.sourceRect.height &&
           a.region == b.region;
}

} // namespace

int main()
{
    struct Case
    {
        const char *name;
        CompareDisplayPlanningInput input;
        bool region;
        int targetWidth;
        int targetHeight;
        CompareDisplayRect sourceRect;
    };

    auto fit = input(4000, 3000, 1000, 600, 0.2);
    fit.visibleSourceRect = {0, 0, 4000, 3000};

    std::vector<Case> cases;
    auto placeholder = input(12000, 8333, 1000, 600, 1.0);
    placeholder.hasWidgetSourceSize = false;
    cases.push_back({"initial placeholder", placeholder, false, 1250, 1250,
                     {0, 0, 12000, 8333}});
    cases.push_back({"normal Fit", fit, false, 1250, 1250, {0, 0, 4000, 3000}});

    auto independent = input(2000, 1000, 1000, 600, 0.5);
    independent.pane = 1;
    independent.fitScales = {0.25, 0.5};
    independent.visibleSourceRect = {0, 0, 2000, 1000};
    cases.push_back({"independent pane Fit", independent, false, 1250, 1250,
                     {0, 0, 2000, 1000}});

    auto uniform = independent;
    uniform.uniformScale = true;
    uniform.currentScale = 0.25;
    uniform.paneScale = 0.25;
    cases.push_back({"uniform scale", uniform, false, 1250, 1250, {0, 0, 2000, 1000}});

    auto firstWheel = fit;
    firstWheel.currentScale = 0.23;
    firstWheel.paneScale = firstWheel.currentScale;
    cases.push_back({"first wheel zoom", firstWheel, false, 1438, 1438,
                     {0, 0, 4000, 3000}});

    auto moderate = fit;
    moderate.currentScale = 0.40;
    moderate.paneScale = moderate.currentScale;
    moderate.visibleSourceRect = {1000, 800, 1000, 800};
    cases.push_back({"moderate zoom", moderate, true, 3125, 2500, {875, 700, 1250, 1000}});

    auto high = fit;
    high.currentScale = 0.90;
    high.paneScale = high.currentScale;
    high.visibleSourceRect = {1800, 1300, 300, 250};
    cases.push_back({"high zoom", high, true, 2104, 1755, {1763, 1269, 374, 312}});

    auto edge = input(10000, 8000, 1000, 600, 0.30);
    edge.visibleSourceRect = {9300, 7300, 700, 500};
    cases.push_back({"pan to edge", edge, true, 3935, 3120, {9213, 7238, 787, 624}});

    auto nearFull = input(1000, 1000, 500, 500, 2.0);
    nearFull.visibleSourceRect = {0, 0, 900, 900};
    cases.push_back({"near-full coverage boundary", nearFull, false, 2500, 2500,
                     {0, 0, 1000, 1000}});

    for (const double dpr : {1.0, 1.25, 1.5, 2.0})
    {
        auto dprCase = input(4000, 3000, 1000, 600, 0.2, dpr);
        dprCase.visibleSourceRect = {0, 0, 4000, 3000};
        const int edge = static_cast<int>(std::ceil(1000.0 * dpr * 1.25));
        cases.push_back({"DPR density", dprCase, false, edge, edge, {0, 0, 4000, 3000}});
    }

    auto staleFit = input(4000, 3000, 1000, 600, 2.0);
    staleFit.uniformScale = true;
    staleFit.fitScales = {0.0, std::numeric_limits<double>::infinity()};
    staleFit.visibleSourceRect = {1000, 800, 500, 400};
    cases.push_back({"invalid stale fit scale", staleFit, false, 2500, 2500,
                     {0, 0, 4000, 3000}});

    auto crop = moderate;
    crop.hasCropOrRotation = true;
    cases.push_back({"crop active", crop, false, 1250, 1250, {0, 0, 4000, 3000}});
    auto rotation = moderate;
    rotation.hasCropOrRotation = true;
    cases.push_back({"rotation active", rotation, false, 1250, 1250, {0, 0, 4000, 3000}});

    auto resize = input(4000, 3000, 1600, 900, 0.30);
    resize.visibleSourceRect = {0, 0, 4000, 3000};
    cases.push_back({"viewport resize", resize, false, 2000, 2000, {0, 0, 4000, 3000}});

    auto largeJpeg = input(12000, 8333, 1200, 800, 0.20);
    largeJpeg.visibleSourceRect = {3000, 2000, 2000, 1500};
    cases.push_back({"large JPEG source-backed pane", largeJpeg, true, 4096, 4096,
                     {2750, 1813, 2500, 1874}});

    int failures = 0;
    for (const auto &test : cases)
    {
        const CompareDisplayPlan plan = mviewer::ui::planCompareDisplay(test.input);
        const bool ok = plan.isValid() && plan.region == test.region &&
                        plan.targetWidth == test.targetWidth &&
                        plan.targetHeight == test.targetHeight &&
                        plan.sourceRect.x == test.sourceRect.x &&
                        plan.sourceRect.y == test.sourceRect.y &&
                        plan.sourceRect.width == test.sourceRect.width &&
                        plan.sourceRect.height == test.sourceRect.height;
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", test.name);
        if (!ok)
        {
            std::printf("  got target=%dx%d rect=%d,%d %dx%d region=%d\n", plan.targetWidth,
                        plan.targetHeight, plan.sourceRect.x, plan.sourceRect.y,
                        plan.sourceRect.width, plan.sourceRect.height, plan.region);
            ++failures;
        }
    }

    const auto duplicateA = mviewer::ui::planCompareDisplay(cases[1].input);
    const auto duplicateB = mviewer::ui::planCompareDisplay(cases[1].input);
    if (!samePlan(duplicateA, duplicateB))
    {
        std::printf("FAIL: duplicate panes keep independent value plans\n");
        ++failures;
    }
    else
        std::printf("PASS: duplicate panes keep independent value plans\n");

    auto paneA = input(12000, 8333, 1200, 800, 0.20);
    paneA.visibleSourceRect = {3000, 2000, 2000, 1500};
    const auto planA1 = mviewer::ui::planCompareDisplay(paneA);
    auto paneB = input(4000, 3000, 1200, 800, 0.30);
    paneB.visibleSourceRect = {1000, 700, 1000, 700};
    (void)mviewer::ui::planCompareDisplay(paneB);
    const auto planA2 = mviewer::ui::planCompareDisplay(paneA);
    if (!samePlan(planA1, planA2))
    {
        std::printf("FAIL: A->B->A replacement does not reuse stale state\n");
        ++failures;
    }
    else
        std::printf("PASS: A->B->A replacement keeps value-plan isolation\n");

    std::printf("=== Compare display planner tests: %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
