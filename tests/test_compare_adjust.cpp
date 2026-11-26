// CompareAdjustController: the shared adjust for both compare panes must reach
// the workspace's own colour context (which is what invalidates the rasters).
#include "compareworkspace.h"
#include "core/image/DisplayColorContext.h"
#include "widgets/displayadjustcontroller.h"

#include <QApplication>
#include <QImage>
#include <QTemporaryDir>

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

    if (g_failures != 0)
    {
        std::cout << "compare_adjust_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_adjust_tests: PASS\n";
    return 0;
}
