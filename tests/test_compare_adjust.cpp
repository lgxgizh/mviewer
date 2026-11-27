// CompareAdjustController: the shared adjust for both compare panes must reach
// the workspace's own colour context (which is what invalidates the rasters).
#include "compareworkspace.h"
#include "core/image/DisplayColorContext.h"
#include "widgets/displayadjustcontroller.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QWidget>

#include <functional>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

QString writePng(QTemporaryDir &dir, const QString &name, const QColor &color)
{
    QImage image(24, 18, QImage::Format_RGB32);
    image.fill(color);
    const QString path = dir.filePath(name);
    return image.save(path, "PNG") ? path : QString();
}

void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    if (!dir.isValid())
        return 1;

    CompareWorkspace ws;
    ws.resize(640, 400);
    ws.show();
    const QString a = writePng(dir, "a.png", QColor(20, 30, 40));
    const QString b = writePng(dir, "b.png", QColor(200, 210, 220));
    ws.setImages({a, b});

    mviewer::ui::DisplayAdjustController controller;
    controller.attach(
        &ws,
        [&ws]() { return ws.displayColorContext().adjust; },
        [&ws](const mviewer::core::DisplayAdjust &adjust)
        {
            auto next = ws.displayColorContext();
            next.adjust = adjust;
            ws.setDisplayColorContext(next);
        });

    CHECK(!controller.isPanelVisible(), "the adjust panel starts hidden");
    controller.toggle();
    CHECK(controller.isPanelVisible(), "toggle shows the panel");

    // The panel only announces changes; the controller must push values back into
    // it after a reset, or the widgets keep showing the pre-reset state.
    QWidget *panel = controller.panelWidget();
    CHECK(panel != nullptr, "the panel widget exists once shown");
    const QList<QSlider *> sliders = panel->findChildren<QSlider *>();
    CHECK(sliders.size() == 2, "the panel has the exposure and gamma sliders");
    if (sliders.size() == 2)
    {
        const int baseExposure = sliders.at(0)->value();
        const int baseGamma = sliders.at(1)->value();
        // Simulate the user dragging both sliders away from neutral.
        sliders.at(0)->setValue(qBound(sliders.at(0)->minimum(), baseExposure + 5,
                                      sliders.at(0)->maximum()));
        sliders.at(1)->setValue(qBound(sliders.at(1)->minimum(), baseGamma - 5,
                                      sliders.at(1)->maximum()));
        CHECK(sliders.at(0)->value() != baseExposure && sliders.at(1)->value() != baseGamma,
              "dragging the sliders moves them (and reaches the host)");
        controller.resetToIdentity();
        CHECK(sliders.at(0)->value() == baseExposure && sliders.at(1)->value() == baseGamma,
              "reset puts the panel sliders back to their neutral positions");
    }

    controller.toggle();
    CHECK(!controller.isPanelVisible(), "toggle hides it again");

    const std::string keyBefore =
        ws.displayColorContext().cacheKey();
    controller.setExposure(1.5);
    const std::string keyAfter = ws.displayColorContext().cacheKey();
    CHECK(ws.displayColorContext().adjust.exposureEv == 1.5,
          "exposure reaches the workspace adjust");
    CHECK(keyBefore != keyAfter, "changing the adjust changes the display cache key");

    controller.setGamma(2.4);
    CHECK(ws.displayColorContext().adjust.gamma == 2.4, "gamma reaches the workspace adjust");

    controller.setAutoStretch(true);
    CHECK(ws.displayColorContext().adjust.autoStretch, "auto-stretch reaches the workspace adjust");

    controller.resetToIdentity();
    const mviewer::core::DisplayAdjust identity = ws.displayColorContext().adjust;
    CHECK(identity.exposureEv == 0.0 && identity.gamma == 1.0 && !identity.autoStretch,
          "reset restores an identity adjust");

    controller.detach();
    controller.setExposure(3.0);
    CHECK(ws.displayColorContext().adjust.exposureEv == 0.0,
          "after detach the controller cannot touch the workspace");

    // Hosts that lay out their own overlays must not get a top-right anchor: it
    // would fight the layout on every resize.
    QWidget host;
    host.resize(420, 320);
    host.show();
    pump(20);
    mviewer::ui::DisplayAdjustController floating;
    floating.setAutoAnchored(false);
    floating.attach(
        &host,
        []() { return mviewer::core::DisplayAdjust{}; },
        [](const mviewer::core::DisplayAdjust &) {});
    floating.toggle();
    QWidget *unanchored = floating.panelWidget();
    CHECK(unanchored != nullptr, "the non-anchored panel was created");
    if (unanchored)
    {
        const QPoint before = unanchored->pos();
        host.resize(680, 480);
        pump(40);
        CHECK(unanchored->pos() == before,
              "without auto-anchor a host resize does not drag the panel around");
    }
    floating.detach();

    if (g_failures != 0)
    {
        std::cout << "compare_adjust_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_adjust_tests: PASS\n";
    return 0;
}
