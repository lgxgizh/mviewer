#include "directorytree.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QMouseEvent>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>

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

    ThumbnailPanel panel;
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

    std::printf("M36 browse and selection contract tests: %s\n",
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
