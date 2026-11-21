// Floating overlay layout (review: 4 overlay collisions + resize drift).
//
// The corner slots must stay disjoint and inside the viewport, and they must
// follow a resize instead of keeping the geometry they were born with.
#include "imageviewer.h"
#include "widgets/displayadjustpanel.h"
#include "widgets/scopewidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
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

bool disjoint(const QRect &a, const QRect &b)
{
    return !a.intersects(b);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;

    QImage source(160, 96, QImage::Format_RGB32);
    source.fill(Qt::darkGray);
    const QString path = tmp.filePath("overlay.png");
    CHECK(source.save(path, "PNG"), "fixture written");

    ImageViewer viewer;
    viewer.resize(800, 600);
    viewer.show();
    pump(30);
    viewer.setImage(path);
    CHECK(waitFor([&] { return viewer.displaySize().isValid(); }), "viewer decoded the fixture");

    viewer.setScopesVisible(true);
    viewer.setSpectrumVisible(true);
    viewer.setAdjustPanelVisible(true);
    pump(50);

    auto *scope = viewer.findChild<mviewer::ui::ScopeWidget *>();
    auto *adjust = viewer.findChild<mviewer::ui::DisplayAdjustPanel *>();
    auto *spectrum = viewer.findChild<QWidget *>(QStringLiteral("spectrumPanel"));
    CHECK(scope && adjust && spectrum, "all three floating overlays exist");
    if (!scope || !adjust || !spectrum)
        return 1;

    const QRect scopeRect = scope->geometry();
    const QRect adjustRect = adjust->geometry();
    const QRect spectrumRect = spectrum->geometry();

    // Canvas chips that own a corner: the histogram card top-left, the frame
    // pill bottom-left. The widgets must step aside for them.
    const int pillHeight = 24;
    const QRect pill(12, viewer.height() - pillHeight - 12, 120, pillHeight);
    CHECK(disjoint(scopeRect, pill), "the scope widget clears the frame pill");
    CHECK(disjoint(adjustRect, pill), "the adjust card clears the frame pill");
    CHECK(scopeRect.top() >= 100, "the scope widget clears the top-left chip stack");
    CHECK(disjoint(scopeRect, adjustRect), "bottom-left and bottom-right do not overlap");
    CHECK(disjoint(scopeRect, spectrumRect), "left and right slots do not overlap");
    CHECK(disjoint(adjustRect, spectrumRect), "right-hand slots do not overlap");
    CHECK(QRect(0, 0, viewer.width(), viewer.height()).contains(scopeRect) &&
              QRect(0, 0, viewer.width(), viewer.height()).contains(adjustRect) &&
              QRect(0, 0, viewer.width(), viewer.height()).contains(spectrumRect),
          "every overlay stays inside the viewport");

    // A resize must re-layout, not leave the panels at their old coordinates.
    const QRect scopeBefore = scope->geometry();
    viewer.resize(500, 380);
    pump(60);
    viewer.layoutFloatingOverlays();
    pump(20);
    const QRect scopeAfter = scope->geometry();
    CHECK(scopeAfter != scopeBefore, "a resize re-lays out the floating overlays");
    CHECK(QRect(0, 0, viewer.width(), viewer.height()).contains(scope->geometry()) &&
              QRect(0, 0, viewer.width(), viewer.height()).contains(adjust->geometry()) &&
              QRect(0, 0, viewer.width(), viewer.height()).contains(spectrum->geometry()),
          "the overlays stay inside the smaller viewport");
    CHECK(
        disjoint(scope->geometry(), QRect(12, viewer.height() - pillHeight - 12, 120, pillHeight)),
        "the scope widget still clears the pill after resizing");

    if (g_failures != 0)
    {
        std::cout << "viewer_overlay_layout_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "viewer_overlay_layout_tests: PASS\n";
    return 0;
}
