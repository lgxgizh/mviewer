#include "directorytree.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRubberBand>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>

// state() is protected on QAbstractItemView. IconMode paints the marquee
// itself, so the regression check needs the view state, not only a QRubberBand.
class GalleryRubberProbe : public ThumbnailPanel
{
  public:
    using ThumbnailPanel::ThumbnailPanel;

    bool isDragSelecting() const
    {
        return state() == DragSelectingState;
    }
    bool isNoState() const
    {
        return state() == NoState;
    }
};

static int g_failures = 0;
#define CHECK(c, m)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (c)                                                                                      \
            std::printf("PASS: %s\n", m);                                                          \
        else                                                                                        \
        {                                                                                           \
            std::printf("FAIL: %s\n", m);                                                          \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

static bool sameSelection(ThumbnailPanel &panel, const QStringList &expected)
{
    QStringList got = panel.selectedPaths();
    QStringList want = expected;
    got.sort();
    want.sort();
    return got == want;
}

static void pumpUntil(const std::function<bool()> &done, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void sendLeftClick(QWidget *target, const QPoint &pos,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), Qt::LeftButton, Qt::LeftButton,
                      modifiers);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), Qt::LeftButton, Qt::NoButton,
                        modifiers);
    QApplication::sendEvent(target, &release);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void sendViewportMouse(QWidget *target, QEvent::Type type, const QPoint &pos,
                              Qt::MouseButton button, Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, QPointF(pos), QPointF(pos), button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static bool rubberBandEnded(const GalleryRubberProbe &panel)
{
    if (!panel.isNoState())
        return false;
    const QList<QRubberBand *> bands = panel.findChildren<QRubberBand *>();
    for (const QRubberBand *band : bands)
    {
        if (band->isVisible())
            return false;
    }
    return true;
}

// Press on empty viewport space and drag across one cell. The start point is
// outside every item so the gesture is a marquee, not an item click.
static bool findSingleRowBand(const ThumbnailPanel &panel, int row, QPoint &press, QPoint &end)
{
    const QRect cell = panel.visualRect(panel.model()->index(row, 0));
    const QRect vp = panel.viewport()->rect();
    if (!cell.isValid() || vp.isEmpty())
        return false;
    const QPoint candidates[] = {
        QPoint(cell.center().x(), cell.bottom() + 6),
        QPoint(cell.center().x(), cell.top() - 6),
        QPoint(cell.right() + 8, cell.center().y()),
        QPoint(cell.left() - 8, cell.center().y()),
    };
    for (const QPoint &start : candidates)
    {
        if (!vp.contains(start) || panel.indexAt(start).isValid())
            continue;
        const QRect band = QRect(start, cell.center()).normalized();
        bool hitsTarget = false;
        bool hitsOther = false;
        for (int r = 0; r < panel.model()->rowCount(); ++r)
        {
            const QRect other = panel.visualRect(panel.model()->index(r, 0));
            if (!other.intersects(band))
                continue;
            if (r == row)
                hitsTarget = true;
            else
                hitsOther = true;
        }
        if (!hitsTarget || hitsOther)
            continue;
        press = start;
        end = cell.center();
        return true;
    }
    return false;
}

static void testGalleryRubberBand(GalleryRubberProbe &panel)
{
    panel.resize(1000, 720);
    panel.show();
    const QModelIndex row0 = panel.model()->index(0, 0);
    const QModelIndex row2 = panel.model()->index(2, 0);
    const QModelIndex row3 = panel.model()->index(3, 0);
    pumpUntil([&panel, &row3] { return !panel.visualRect(row3).isEmpty(); });
    CHECK(!panel.visualRect(row0).isEmpty() && !panel.visualRect(row3).isEmpty(),
          "rubber-band cells are laid out");

    QPoint press;
    QPoint end;
    CHECK(findSingleRowBand(panel, 0, press, end),
          "empty viewport point can marquee the first cell");

    QWidget *viewport = panel.viewport();
    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(panel.isDragSelecting(), "empty-area drag enters rubber-band selection");
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    const QString first = panel.pathList().at(0);
    CHECK(sameSelection(panel, QStringList{first}), "rubber band selects the cell inside the rect");
    CHECK(rubberBandEnded(panel), "releasing the mouse ends the rubber band");

    const QPoint elsewhere = panel.visualRect(row3).center();
    sendViewportMouse(viewport, QEvent::MouseMove, elsewhere, Qt::NoButton, Qt::NoButton);
    CHECK(sameSelection(panel, QStringList{first}),
          "a move with no button pressed does not change the marquee selection");
    CHECK(rubberBandEnded(panel), "the rubber band stays ended after the pointer moves");

    sendLeftClick(viewport, panel.visualRect(row0).center());
    CHECK(sameSelection(panel, QStringList{first}), "item click still selects that image");
    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
    CHECK(panel.selectedPaths().isEmpty(), "plain click on empty area clears the selection");
    CHECK(rubberBandEnded(panel), "an empty click does not leave a rubber band");

    sendLeftClick(viewport, panel.visualRect(row0).center());
    QPoint ctrlPress;
    QPoint ctrlEnd;
    CHECK(findSingleRowBand(panel, 2, ctrlPress, ctrlEnd), "empty point can marquee a later cell");
    sendViewportMouse(viewport, QEvent::MouseButtonPress, ctrlPress, Qt::LeftButton, Qt::LeftButton,
                      Qt::ControlModifier);
    sendViewportMouse(viewport, QEvent::MouseMove, ctrlEnd, Qt::NoButton, Qt::LeftButton,
                      Qt::ControlModifier);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, ctrlEnd, Qt::LeftButton, Qt::NoButton,
                      Qt::ControlModifier);
    CHECK(sameSelection(panel, QStringList{first, panel.pathList().at(2)}),
          "Ctrl+drag adds the marqueed cell and keeps the previous selection");
    CHECK(rubberBandEnded(panel), "Ctrl+drag release ends the rubber band");

    sendLeftClick(viewport, panel.visualRect(row0).center());
    sendViewportMouse(viewport, QEvent::MouseButtonPress, ctrlPress, Qt::LeftButton, Qt::LeftButton,
                      Qt::ShiftModifier);
    sendViewportMouse(viewport, QEvent::MouseMove, ctrlEnd, Qt::NoButton, Qt::LeftButton,
                      Qt::ShiftModifier);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, ctrlEnd, Qt::LeftButton, Qt::NoButton,
                      Qt::ShiftModifier);
    CHECK(sameSelection(panel, QStringList{first, panel.pathList().at(2)}),
          "Shift+drag keeps the existing cell and adds the marqueed cell");
    CHECK(rubberBandEnded(panel), "Shift+drag release ends the rubber band");

    sendLeftClick(viewport, panel.visualRect(row0).center());
    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton,
                      Qt::ControlModifier);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
                      Qt::ControlModifier);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
                      Qt::ControlModifier);
    CHECK(panel.selectedPaths().isEmpty(),
          "Ctrl+drag over an already selected cell toggles it off like Explorer");
    CHECK(rubberBandEnded(panel), "toggle marquee release ends the rubber band");

    sendLeftClick(viewport, panel.visualRect(row3).center());
    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    CHECK(sameSelection(panel, QStringList{first}),
          "plain marquee replaces the previous selection");
    sendLeftClick(viewport, panel.visualRect(row2).center(), Qt::ShiftModifier);
    CHECK(sameSelection(panel, QStringList{panel.pathList().at(0), panel.pathList().at(1),
                                           panel.pathList().at(2)}),
          "Shift-click after a marquee extends from the last selected cell");

    sendLeftClick(viewport, panel.visualRect(row0).center());
    sendViewportMouse(viewport, QEvent::MouseButtonPress, panel.visualRect(row0).center(),
                      Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, panel.visualRect(row2).center(), Qt::NoButton,
                      Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, panel.visualRect(row2).center(),
                      Qt::LeftButton, Qt::NoButton);
    CHECK(sameSelection(panel, QStringList{first}),
          "dragging from an item does not rubber-band select other cells");
    CHECK(rubberBandEnded(panel), "an item drag does not leave a rubber band");

    int opened = 0;
    QObject::connect(&panel, &ThumbnailPanel::itemDoubleClicked,
                     [&opened](const QString &) { ++opened; });
    const QPointF cell = QPointF(panel.visualRect(row0).center());
    QMouseEvent dbl(QEvent::MouseButtonDblClick, cell, cell, Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(viewport, &dbl);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(opened == 1, "double-click on a cell still opens that image");

    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(panel.isDragSelecting(), "a second marquee enters rubber-band selection");
    const QStringList beforeEsc = panel.selectedPaths();
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&panel, &esc);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(rubberBandEnded(panel), "Escape ends the rubber band");
    CHECK(sameSelection(panel, beforeEsc), "Escape keeps the marquee selection");
    sendViewportMouse(viewport, QEvent::MouseMove, elsewhere, Qt::NoButton, Qt::NoButton);
    CHECK(sameSelection(panel, beforeEsc), "moving after Escape does not change the selection");

    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(panel.isDragSelecting(), "focus-loss marquee is in rubber-band selection");
    const QStringList beforeFocus = panel.selectedPaths();
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(&panel, &focusOut);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(rubberBandEnded(panel), "focus loss ends the rubber band");
    CHECK(sameSelection(panel, beforeFocus), "focus loss keeps the marquee selection");
    sendViewportMouse(viewport, QEvent::MouseMove, elsewhere, Qt::NoButton, Qt::NoButton);
    CHECK(sameSelection(panel, beforeFocus),
          "moving after focus loss does not change the selection");

    sendViewportMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendViewportMouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(panel.isDragSelecting(), "outside-release marquee is in rubber-band selection");
    const QStringList beforeOutside = panel.selectedPaths();
    sendViewportMouse(viewport, QEvent::MouseButtonRelease, QPoint(-40, -40), Qt::LeftButton,
                      Qt::NoButton);
    CHECK(rubberBandEnded(panel), "releasing outside the viewport ends the rubber band");
    CHECK(sameSelection(panel, beforeOutside), "an outside release keeps the marquee selection");
    sendViewportMouse(viewport, QEvent::MouseMove, elsewhere, Qt::NoButton, Qt::NoButton);
    CHECK(sameSelection(panel, beforeOutside),
          "moving after an outside release does not change the selection");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid(), "selection fixture temp directory is valid");
    const QString gallery = temp.path() + "/gallery";
    CHECK(QDir().mkpath(gallery), "selection fixture directory is created");
    for (int i = 0; i < 4; ++i)
    {
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(QColor(20 + i * 40, 30, 40));
        CHECK(image.save(gallery + QStringLiteral("/image%1.png").arg(i)),
              "selection fixture image is written");
    }

    GalleryRubberProbe panel;
    SelectionModel selection;
    panel.setSelectionModel(&selection);
    panel.resize(960, 320);
    panel.show();
    panel.setDirectory(gallery);
    pumpUntil([&panel] { return panel.pathList().size() == 4; });
    CHECK(panel.pathList().size() == 4, "thumbnail panel lists all fixtures");

    int opens = 0;
    QObject::connect(&panel, &ThumbnailPanel::itemClicked,
                     [&opens](const QString &) { ++opens; });
    const QModelIndex first = panel.model()->index(0, 0);
    const QModelIndex second = panel.model()->index(1, 0);
    pumpUntil([&panel, &first] { return !panel.visualRect(first).isEmpty(); });
    CHECK(!panel.visualRect(first).isEmpty(), "thumbnail selection cell is laid out");

    sendLeftClick(panel.viewport(), panel.visualRect(first).center());
    CHECK(panel.selectedPaths().size() == 1, "plain click selects one image");
    CHECK(opens == 1, "plain click opens the focused image");

    sendLeftClick(panel.viewport(), panel.visualRect(second).center(), Qt::ControlModifier);
    CHECK(panel.selectedPaths().size() == 2,
          "Ctrl-click preserves the existing image selection");
    CHECK(selection.selection().size() == 2,
          "Ctrl-click publishes the complete selection model state");
    CHECK(opens == 1, "Ctrl-click does not open and collapse the selection");

    const QStringList ordered = panel.pathList();
    const QString &imageA = ordered.at(0);
    const QString &imageB = ordered.at(1);
    const QString &imageC = ordered.at(2);
    const QString &imageD = ordered.at(3);
    const QModelIndex indexA = panel.model()->index(0, 0);
    const QModelIndex indexB = panel.model()->index(1, 0);
    const QModelIndex indexC = panel.model()->index(2, 0);
    const QModelIndex indexD = panel.model()->index(3, 0);
    pumpUntil([&panel, &indexD] { return !panel.visualRect(indexD).isEmpty(); });
    CHECK(!panel.visualRect(indexD).isEmpty(), "later thumbnail cells are laid out");

    sendLeftClick(panel.viewport(), panel.visualRect(indexA).center());
    const bool onlyA = sameSelection(panel, QStringList{imageA});
    CHECK(onlyA, "plain click selects only the clicked image");

    sendLeftClick(panel.viewport(), panel.visualRect(indexC).center(), Qt::ShiftModifier);
    const bool rangeAC = sameSelection(panel, QStringList{imageA, imageB, imageC});
    CHECK(rangeAC, "Shift-click selects the inclusive anchor range A through C");

    sendLeftClick(panel.viewport(), panel.visualRect(indexB).center(), Qt::ShiftModifier);
    const bool rangeAB = sameSelection(panel, QStringList{imageA, imageB});
    CHECK(rangeAB, "shorter Shift-click selects exactly A through B");
    CHECK(!panel.selectedPaths().contains(imageC),
          "shorter Shift-click drops the image outside the anchor range");

    sendLeftClick(panel.viewport(), panel.visualRect(indexD).center(), Qt::ControlModifier);
    const bool toggledD = sameSelection(panel, QStringList{imageA, imageB, imageD});
    CHECK(toggledD, "Ctrl-click toggles only the clicked image");

    sendLeftClick(panel.viewport(), panel.visualRect(indexB).center(),
                  Qt::ControlModifier | Qt::ShiftModifier);
    const bool addedRange = sameSelection(panel, QStringList{imageA, imageB, imageC, imageD});
    CHECK(addedRange, "Ctrl+Shift-click adds the anchor range and keeps the outside image");

    sendLeftClick(panel.viewport(), panel.visualRect(indexC).center());
    const bool onlyC = sameSelection(panel, QStringList{imageC});
    CHECK(onlyC, "a later plain click selects only the clicked image");

    testGalleryRubberBand(panel);

    std::printf("M36 browse and selection contract tests: %s\n",
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
