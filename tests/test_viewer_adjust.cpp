// Viewer display-adjust panel (G): the controls mutate a DisplayAdjust and the
// viewer pushes it into the display colour context, which is what makes the
// rasters and tiles re-materialize.
#include "imageviewer.h"

#include <QApplication>
#include <QElapsedTimer>
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

void pump(int ms = 20)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return 1;

    QImage source(160, 96, QImage::Format_RGB32);
    source.fill(Qt::darkGray);
    const QString path = tmp.filePath("adjust.png");
    CHECK(source.save(path, "PNG"), "fixture written");

    ImageViewer viewer;
    viewer.resize(640, 400);
    viewer.show();
    pump(30);
    viewer.setImage(path);
    CHECK(waitFor([&] { return viewer.displaySize().isValid(); }), "viewer decoded the fixture");

    const QString before = QString::fromStdString(viewer.displayColorContext().cacheKey());
    viewer.setAdjustPanelVisible(true);
    pump(20);
    CHECK(viewer.isAdjustPanelVisible(), "G shows the display-adjust panel");
    CHECK(viewer.displayAdjust().exposureEv == 0.0, "exposure starts neutral");

    viewer.displayAdjust().exposureEv = 1.0;
    viewer.applyDisplayAdjust();
    pump(20);
    const QString after = QString::fromStdString(viewer.displayColorContext().cacheKey());
    CHECK(before != after, "applying the adjustment changes the display cache key");
    CHECK(viewer.displayAdjust().exposureEv == 1.0, "the adjustment sticks");

    viewer.displayAdjust() = mviewer::core::DisplayAdjust{};
    viewer.applyDisplayAdjust();
    pump(20);
    CHECK(QString::fromStdString(viewer.displayColorContext().cacheKey()) == before,
          "resetting restores the original cache key");

    viewer.setAdjustPanelVisible(false);
    pump(10);
    CHECK(!viewer.isAdjustPanelVisible(), "the panel hides again");

    // Asking to show an already visible panel must not flip it off.
    viewer.setAdjustPanelVisible(true);
    pump(10);
    CHECK(viewer.isAdjustPanelVisible(), "the panel is shown");
    viewer.setAdjustPanelVisible(true);
    pump(10);
    CHECK(viewer.isAdjustPanelVisible(), "re-asking to show keeps the panel visible");
    viewer.setAdjustPanelVisible(false);
    pump(10);
    CHECK(!viewer.isAdjustPanelVisible(), "and it still hides on request");

    if (g_failures != 0)
    {
        std::cout << "viewer_adjust_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "viewer_adjust_tests: PASS\n";
    return 0;
}
