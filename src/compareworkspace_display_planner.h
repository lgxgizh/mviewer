#pragma once

#include <vector>

namespace mviewer::ui
{

struct CompareDisplayRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isValid() const { return width > 0 && height > 0; }
};

// Snapshot of the state needed to choose a source-backed display request.
// It deliberately contains no QWidget, QObject, or decoder state: the UI
// collects this value on the UI thread and the planner returns another value.
struct CompareDisplayPlanningInput
{
    int pane = -1;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    double devicePixelRatio = 1.0;
    double paneScale = 1.0;
    double currentScale = 1.0;
    std::vector<double> fitScales;
    bool uniformScale = false;
    bool hasWidgetSourceSize = false;
    bool hasCropOrRotation = false;
    CompareDisplayRect visibleSourceRect;
};

struct CompareDisplayPlan
{
    int targetWidth = 0;
    int targetHeight = 0;
    CompareDisplayRect sourceRect;
    bool region = false;

    bool isValid() const
    {
        return targetWidth > 0 && targetHeight > 0 && sourceRect.isValid();
    }
};

CompareDisplayPlan planCompareDisplay(const CompareDisplayPlanningInput &input);

} // namespace mviewer::ui
