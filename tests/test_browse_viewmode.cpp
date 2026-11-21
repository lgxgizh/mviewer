// Browse toolbar view-mode segment mapping (split out of
// test_browse_acceptance.cpp to keep that file under the size cap).
#include "test_browse_viewmode.h"
#include "mainwindow.h"
#include "thumbnailpanel.h"
#include "widgets/ViewModeSegment.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QSettings>
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
    } while (0)

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}
} // namespace

// The toolbar's list-icon segment must open the Details view (Ctrl+3), not
// the plain List view (Ctrl+2), and every segment must stay in sync with the
// Ctrl+1..6 shortcuts.
int runViewModeSegmentTests()
{
    std::cout << "── Browse: view-mode segment mapping ──\n";
    QSettings().clear();
    MainWindow w;
    w.resize(1100, 750);
    w.show();
    pump(80);
    auto *segment = w.findChild<ViewModeSegment *>(QStringLiteral("viewModeSegment"));
    auto *combo = w.findChild<QComboBox *>(QStringLiteral("thumbnailViewModeCombo"));
    auto *panel = w.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    CHECK(segment && combo && panel, "segment, view-mode combo and panel exist");
    if (!segment || !combo || !panel)
        return g_failures;
    QAbstractButton *details = segment->buttonForData(int(ThumbnailPanel::Details));
    CHECK(details != nullptr, "the list-icon segment is bound to Details");
    CHECK(segment->buttonForData(int(ThumbnailPanel::List)) == nullptr,
          "no segment selects the plain List view");
    const auto pressCtrl = [&w](Qt::Key key)
    {
        QKeyEvent press(QEvent::KeyPress, key, Qt::ControlModifier);
        QApplication::sendEvent(&w, &press);
        pump(20);
    };
    pressCtrl(Qt::Key_1);
    CHECK(panel->viewMode() == ThumbnailPanel::Thumbnail, "Ctrl+1 selects Grid");
    CHECK(segment->checkedData() == int(ThumbnailPanel::Thumbnail), "Ctrl+1 lights Grid");
    if (details)
        details->click();
    pump(20);
    CHECK(panel->viewMode() == ThumbnailPanel::Details, "details segment selects Details");
    CHECK(combo->currentData().toInt() == int(ThumbnailPanel::Details),
          "details segment drives the combo to Details");
    CHECK(details && details->isChecked(), "details segment stays checked after the click");
    pressCtrl(Qt::Key_1);
    pressCtrl(Qt::Key_3);
    CHECK(panel->viewMode() == ThumbnailPanel::Details, "Ctrl+3 selects Details");
    CHECK(segment->checkedData() == int(ThumbnailPanel::Details), "Ctrl+3 lights Details");
    pressCtrl(Qt::Key_2);
    CHECK(panel->viewMode() == ThumbnailPanel::List, "Ctrl+2 selects List");
    CHECK(segment->checkedData() == int(ThumbnailPanel::Details),
          "Ctrl+2 lights the list-icon segment");
    pressCtrl(Qt::Key_4);
    CHECK(segment->checkedData() == int(ThumbnailPanel::Filmstrip), "Ctrl+4 lights Filmstrip");
    pressCtrl(Qt::Key_5);
    CHECK(segment->checkedData() == int(ThumbnailPanel::Thumbnail),
          "Ctrl+5 (small icons) lights Grid");
    if (QAbstractButton *film = segment->buttonForData(int(ThumbnailPanel::Filmstrip)))
        film->click();
    pump(20);
    CHECK(panel->viewMode() == ThumbnailPanel::Filmstrip, "filmstrip segment selects Filmstrip");
    return g_failures;
}
