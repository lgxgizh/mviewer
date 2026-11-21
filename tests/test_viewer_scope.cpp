// Viewer line-profile and video-scope tooling:
//  * W shows the scope panel, and further presses cycle its mode;
//  * L arms the line profile and a drag commits a line in image coordinates.
#include "imageviewer.h"
#include "widgets/scopewidget.h"

using mviewer::ui::ScopeWidget;

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
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

void sendDrag(ImageViewer &viewer, const QPointF &a, const QPointF &b)
{
    QMouseEvent press(QEvent::MouseButtonPress, a, viewer.mapToGlobal(a), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &press);
    QMouseEvent move(QEvent::MouseMove, b, viewer.mapToGlobal(b), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&viewer, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, b, viewer.mapToGlobal(b), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &release);
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

    QImage source(200, 120, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixel(x, y, qRgb(x * 255 / source.width(), y * 255 / source.height(), 128));
    const QString path = tmp.filePath("scope.png");
    CHECK(source.save(path, "PNG"), "fixture written");

    ImageViewer viewer;
    viewer.resize(640, 400);
    viewer.show();
    pump(30);
    viewer.setImage(path);
    CHECK(waitFor([&] { return viewer.displaySize().isValid(); }), "viewer decoded the fixture");

    ScopeWidget *scope = nullptr;
    CHECK(!viewer.isScopesVisible(), "scopes start hidden");

    viewer.setScopesVisible(true);
    pump(20);
    scope = viewer.findChild<ScopeWidget *>();
    CHECK(scope != nullptr && scope->isVisible(), "W creates and shows the scope panel");
    CHECK(scope->mode() == ScopeWidget::Mode::LumaWaveform, "default scope is the luma waveform");

    viewer.cycleScopeMode();
    pump(10);
    CHECK(scope->mode() == ScopeWidget::Mode::RgbParade, "cycling switches to the RGB parade");
    viewer.cycleScopeMode();
    pump(10);
    CHECK(scope->mode() == ScopeWidget::Mode::Vectorscope, "cycling switches to the vectorscope");

    // A committed drag turns the panel into the line profile.
    viewer.setProfileMode(true);
    CHECK(viewer.isProfileMode(), "L arms the line profile");
    const QPointF a(viewer.width() * 0.3, viewer.height() * 0.5);
    const QPointF b(viewer.width() * 0.7, viewer.height() * 0.5);
    sendDrag(viewer, a, b);
    pump(20);
    CHECK(scope->hasProfileLine(), "the drag commits a profile line");
    CHECK(scope->mode() == ScopeWidget::Mode::LineProfile,
          "the panel follows onto the line profile");

    viewer.setProfileMode(false);
    pump(10);
    CHECK(!scope->hasProfileLine(), "leaving the tool clears the committed line");
    viewer.setScopesVisible(false);
    pump(10);
    CHECK(!scope->isVisible(), "the scope panel hides again");

    if (g_failures != 0)
    {
        std::cout << "viewer_scope_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "viewer_scope_tests: PASS\n";
    return 0;
}
