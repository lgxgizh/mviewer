// Compare display debounce merge and pixel-grid gating: the pixel grid must not
// be drawn over a soft / lower-density stand-in. Fixture size matches the user report:
// two 4096x3072 JPEGs at 744% / 855% zoom.
#include "compareworkspace_display_planner.h"
#include "compareworkspace_display_state.h"
#include "core/analysis/PixelGrid.h"
#include "widgets/rawimageview.h"
#include <QApplication>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{

using mviewer::ui::mergeLodRefreshPane;

bool expect(const char *name, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

mviewer::ui::CompareDisplayPlanningInput highZoomInput(double scale)
{
    mviewer::ui::CompareDisplayPlanningInput in;
    in.sourceWidth = 4096;
    in.sourceHeight = 3072;
    in.viewportWidth = 950;
    in.viewportHeight = 1000;
    in.devicePixelRatio = 1.0;
    in.currentScale = scale;
    in.paneScale = scale;
    in.hasWidgetSourceSize = true;
    // Centered viewport in source pixels.
    const int w = static_cast<int>(in.viewportWidth / scale);
    const int h = static_cast<int>(in.viewportHeight / scale);
    in.visibleSourceRect = {(4096 - w) / 2, (3072 - h) / 2, w, h};
    return in;
}

} // namespace

int main()
{
    int fails = 0;

    // Sync zoom: pane 0 then pane 1 emit scaleChanged inside one debounce.
    if (!expect("two panes in one debounce window refresh all",
                mergeLodRefreshPane(true, 0, 1) == -1))
        ++fails;
    if (!expect("same pane twice stays that pane", mergeLodRefreshPane(true, 1, 1) == 1))
        ++fails;
    if (!expect("first request in a window takes its pane", mergeLodRefreshPane(false, -1, 1) == 1))
        ++fails;
    if (!expect("an all-pane request is never narrowed", mergeLodRefreshPane(true, -1, 0) == -1))
        ++fails;

    // Batch pane carry-over and the cheap/sharp choice moved into the display
    // state machine: see test_compare_display_state.cpp.

    // 4096x3072 threshold math. Both 744% and 855% plan a density-1 region
    // raster (no cache mip / 640 cheap LOD is involved for the sharp plan).
    for (const double scale : {7.44, 8.55})
    {
        const auto plan = mviewer::ui::planSharpCompareDisplay(highZoomInput(scale));
        const bool ok = plan.isValid() && plan.region &&
                        plan.targetWidth == plan.sourceRect.width &&
                        plan.targetHeight == plan.sourceRect.height;
        if (!expect(scale < 8.0 ? "744% sharp plan is a 1:1 region raster"
                                : "855% sharp plan is a 1:1 region raster",
                    ok))
            ++fails;
        if (ok && !expect("sharp region raster keeps the pixel grid",
                          mviewer::pixelGridMatchesRaster(plan.targetWidth, plan.targetHeight,
                                                          plan.sourceRect.width,
                                                          plan.sourceRect.height, false)))
            ++fails;
    }
    if (!expect("pixel grid threshold sits between 744% and 855%",
                !mviewer::pixelGridVisible(7.44) && mviewer::pixelGridVisible(8.55)))
        ++fails;
    // Cheap 640 provisional of a 4096x3072 frame (640x480 over the full
    // frame) or a warm thumb must not get a pixel lattice drawn over it.
    if (!expect("no grid over 640x480 cheap LOD of 4096x3072",
                !mviewer::pixelGridMatchesRaster(640, 480, 4096, 3072, false)))
        ++fails;
    if (!expect("no grid while softLoading",
                !mviewer::pixelGridMatchesRaster(4096, 3072, 4096, 3072, true)))
        ++fails;
    if (!expect("rounding slack keeps the grid on a 1:1 raster",
                mviewer::pixelGridMatchesRaster(139, 146, 139, 147, false)))
        ++fails;

    int argc = 0;
    QApplication app(argc, nullptr);
    RawImageView view;
    if (!expect("raw image view pixel grid is disabled by default", !view.isPixelGridEnabled()))
        ++fails;
    view.setPixelGridEnabled(true);
    if (!expect("raw image view pixel grid can be enabled", view.isPixelGridEnabled()))
        ++fails;

    std::printf("%d failure(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
