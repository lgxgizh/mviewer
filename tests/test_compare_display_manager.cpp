#include "compare_display_manager.h"

#include <QApplication>
#include <iostream>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

using namespace mviewer::ui;

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    CompareDisplayManager dm;
    CHECK(dm.columns() == 2, "default columns is 2");
    CHECK(!dm.uniformScale(), "default uniformScale is false");
    CHECK(dm.syncZoom(), "default syncZoom is true");
    CHECK(dm.syncDrag(), "default syncDrag is true");
    CHECK(dm.sharedZoomRatio() == 1.0, "default sharedZoomRatio is 1.0");

    // 1. Column modification
    bool colsFired = false;
    QObject::connect(&dm, &CompareDisplayManager::columnsChanged,
                     [&](int c) { colsFired = (c == 4); });
    dm.setColumns(4);
    CHECK(colsFired && dm.columns() == 4, "columns changed to 4");
    dm.setColumns(0); // clamps to 1
    CHECK(dm.columns() == 1, "columns clamps to min 1");

    // 2. Uniform scale toggling & signals
    bool uniformFired = false;
    bool fitRequested = false;
    QObject::connect(&dm, &CompareDisplayManager::uniformScaleToggled,
                     [&](bool on) { uniformFired = on; });
    QObject::connect(&dm, &CompareDisplayManager::fitRequested,
                     [&]() { fitRequested = true; });
    dm.setUniformScale(true);
    CHECK(uniformFired && dm.uniformScale(), "uniformScale toggled true");
    CHECK(fitRequested, "fitRequested emitted on uniformScale change");

    // 3. Sync toggles
    bool syncZoomFired = false;
    QObject::connect(&dm, &CompareDisplayManager::syncZoomToggled,
                     [&](bool on) { syncZoomFired = !on; });
    dm.setSyncZoom(false);
    CHECK(syncZoomFired && !dm.syncZoom(), "syncZoom toggled false");

    bool syncDragFired = false;
    QObject::connect(&dm, &CompareDisplayManager::syncDragToggled,
                     [&](bool on) { syncDragFired = !on; });
    dm.setSyncDrag(false);
    CHECK(syncDragFired && !dm.syncDrag(), "syncDrag toggled false");

    // 4. Pure geometry computations
    const double fit1 = CompareDisplayManager::computeFitScale(1000, 800, 2000, 1600);
    CHECK(std::abs(fit1 - 0.5) < 1e-9, "exact fit scale 0.5");

    const double fit2 = CompareDisplayManager::computeFitScale(QSize(1000, 800), QSize(500, 800));
    CHECK(std::abs(fit2 - 1.0) < 1e-9, "fit scale aspect clamped to 1.0");

    // Invalid dimensions return 1.0 fallback
    CHECK(CompareDisplayManager::computeFitScale(0, 800, 2000, 1600) == 1.0, "zero viewW fallback");
    CHECK(CompareDisplayManager::computeFitScale(1000, 0, 2000, 1600) == 1.0, "zero viewH fallback");

    // Uniform fit scale across multiple panes
    std::vector<QSize> views = {QSize(800, 600), QSize(800, 600)};
    std::vector<QSize> imgs = {QSize(1600, 1200), QSize(3200, 2400)};
    // fit pane 0: 800/1600 = 0.5, pane 1: 800/3200 = 0.25 -> uniform fit = 0.25
    const double uniFit = CompareDisplayManager::computeUniformFitScale(views, imgs);
    CHECK(std::abs(uniFit - 0.25) < 1e-9, "uniform fit scale takes min 0.25");

    // Grid layout rows/cols
    auto [r1, c1] = CompareDisplayManager::computeGridRowsCols(2, 2);
    CHECK(r1 == 1 && c1 == 2, "2 images in 2 cols is 1 row");

    auto [r2, c2] = CompareDisplayManager::computeGridRowsCols(3, 2);
    CHECK(r2 == 2 && c2 == 2, "3 images in 2 cols is 2 rows");

    auto [r3, c3] = CompareDisplayManager::computeGridRowsCols(6, 3);
    CHECK(r3 == 2 && c3 == 3, "6 images in 3 cols is 2 rows");

    std::cout << "All CompareDisplayManager tests passed successfully." << std::endl;
    return 0;
}
