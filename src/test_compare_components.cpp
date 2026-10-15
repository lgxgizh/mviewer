#include "compare_diff_renderer.h"
#include "compare_display_manager.h"
#include "compare_roi_controller.h"
#include "compare_session_state.h"
#include "compareworkspace.h"

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
using namespace mviewer::domain;

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // 1. CompareSessionState tests
    {
        CompareSessionState state;
        CHECK(state.isEmpty(), "initial session state is empty");
        CHECK(state.isSyncEnabled(), "initial sync is enabled");
        CHECK(state.layoutPreset() == 2, "initial layout preset is 2");

        bool pathsEmitted = false;
        bool pathsCountOk = false;
        QObject::connect(&state, &CompareSessionState::pathsChanged,
                         [&](const QStringList &p)
                         {
                             pathsEmitted = true;
                             pathsCountOk = (p.size() == 2);
                         });

        state.setComparePaths({"pathA.png", "pathB.png"});
        CHECK(pathsEmitted, "pathsChanged was emitted");
        CHECK(pathsCountOk, "pathsChanged has 2 items");
        CHECK(state.count() == 2, "state.count() == 2");
        CHECK(state.frameIndices().size() == 2, "frame indices sized to 2");

        bool syncToggled = false;
        QObject::connect(&state, &CompareSessionState::syncToggled,
                         [&](bool enabled) { syncToggled = !enabled; });
        state.setSyncEnabled(false);
        CHECK(syncToggled, "syncToggled signal fired on disable");

        // Test roundtrip to domain::CompareSession
        Selection roi;
        roi.x = 10;
        roi.y = 20;
        roi.width = 100;
        roi.height = 80;
        state.setCurrentROI(roi);
        state.setSharedScale(2.5);
        state.setSharedOffset(QPointF(15.0, 30.0));
        state.setLayoutPreset(4);
        state.setAutoAlignBeforeDiff(true);

        CompareSession domainSession = state.toDomainSession();
        CHECK(domainSession.imageIds.size() == 2, "domainSession has 2 images");
        CHECK(domainSession.sharedScale == 2.5, "domainSession sharedScale == 2.5");
        CHECK(domainSession.sharedOffsetX == 15.0, "domainSession sharedOffsetX == 15.0");
        CHECK(domainSession.layoutIndex == 4, "domainSession layoutIndex == 4");
        CHECK(domainSession.selection.w == 100, "domainSession selection w == 100");

        CompareSessionState stateRestored;
        stateRestored.restoreFromDomainSession(domainSession);
        CHECK(stateRestored.count() == 2, "restored count is 2");
        CHECK(stateRestored.comparePaths()[0] == "pathA.png", "restored path matches");
        CHECK(stateRestored.sharedScale() == 2.5, "restored scale matches");
        CHECK(stateRestored.sharedOffset().x() == 15.0, "restored offset matches");
        CHECK(stateRestored.layoutPreset() == 4, "restored layout preset matches");
        CHECK(stateRestored.currentROI().width == 100, "restored ROI matches");
    }

    // 2. CompareRoiController tests
    {
        CompareRoiController controller;
        CHECK(!controller.hasValidROI(), "initial ROI is empty");
        CHECK(controller.measurementState() == ROIMeasurementState::Idle,
              "initial measurement state is Idle");

        bool roiChangedEmitted = false;
        bool roiWidthOk = false;
        QObject::connect(&controller, &CompareRoiController::roiChanged,
                         [&](const Selection &sel)
                         {
                             roiChangedEmitted = true;
                             roiWidthOk = (sel.width == 50);
                         });

        Selection sel;
        sel.x = 10;
        sel.y = 10;
        sel.width = 50;
        sel.height = 40;
        controller.setROI(sel);
        CHECK(roiChangedEmitted, "roiChanged emitted on setROI");
        CHECK(roiWidthOk, "roiChanged sel.width == 50");
        CHECK(controller.hasValidROI(), "hasValidROI is true");

        // Nudge ROI
        controller.nudgeROI(5, -2, 1000, 1000);
        CHECK(controller.currentROI().x == 15, "nudge moved x to 15");
        CHECK(controller.currentROI().y == 8, "nudge moved y to 8");

        // Nudge ROI boundary clamp
        controller.nudgeROI(2000, 2000, 200, 200);
        CHECK(controller.currentROI().x == 150, "nudge clamped x to 200-50=150");
        CHECK(controller.currentROI().y == 160, "nudge clamped y to 200-40=160");

        // Measurement state
        bool stateEmitted = false;
        QObject::connect(&controller, &CompareRoiController::measurementStateChanged,
                         [&](ROIMeasurementState s)
                         {
                             stateEmitted = (s == ROIMeasurementState::Measuring);
                         });
        controller.setMeasurementState(ROIMeasurementState::Measuring);
        CHECK(stateEmitted, "measurementStateChanged emitted");

        // Clear ROI
        bool clearedEmitted = false;
        QObject::connect(&controller, &CompareRoiController::roiCleared,
                         [&]() { clearedEmitted = true; });
        controller.clearROI();
        CHECK(clearedEmitted, "roiCleared emitted");
        CHECK(!controller.hasValidROI(), "hasValidROI is false after clear");
    }

    // 3. CompareWorkspace component wiring and mediation tests
    {
        CompareWorkspace workspace;
        CHECK(workspace.sessionState() != nullptr, "sessionState component instantiated");
        CHECK(workspace.roiController() != nullptr, "roiController component instantiated");
        CHECK(workspace.diffRenderer() != nullptr, "diffRenderer component instantiated");
        CHECK(workspace.displayManager() != nullptr, "displayManager component instantiated");

        // ROI mediation
        Selection roi;
        roi.x = 25;
        roi.y = 35;
        roi.width = 120;
        roi.height = 90;
        workspace.applyROI(roi);
        CHECK(workspace.roiController()->currentROI() == roi,
              "applyROI synchronizes with roiController");
        CHECK(workspace.currentROI() == roi, "currentROI returns controller ROI");

        workspace.roiController()->clearROI();
        CHECK(!workspace.roiController()->hasValidROI(),
              "clearROI on controller clears ROI");
        CHECK(workspace.currentROI().isEmpty(),
              "workspace.currentROI is empty after clear");

        // Bidirectional test: workspace.clearROI() must also clear roiController
        workspace.applyROI(roi);
        CHECK(workspace.roiController()->hasValidROI(), "controller has valid ROI after applyROI");
        workspace.clearROI();
        CHECK(!workspace.roiController()->hasValidROI(), "workspace.clearROI clears roiController");
        CHECK(workspace.currentROI().isEmpty(),
              "workspace.currentROI is empty after workspace.clearROI");

        // DiffRenderer threshold mediation
        workspace.diffRenderer()->setThreshold(64);
        CHECK(workspace.diffRenderer()->threshold() == 64, "diffRenderer threshold updated");

        // SessionState navigation mediation
        workspace.setNavWindow(4);
        CHECK(workspace.sessionState()->navWindow() == 4,
              "setNavWindow synchronizes with sessionState");

        workspace.setImagePool({"test1.png", "test2.png"});
        CHECK(workspace.sessionState()->allPoolPaths().size() == 2,
              "setImagePool synchronizes with sessionState");
    }

    std::cout << "All Compare component tests passed!" << std::endl;
    return 0;
}
