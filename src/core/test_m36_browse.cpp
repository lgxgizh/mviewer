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
        if (!(c))                                                                                   \
        {                                                                                           \
            std::printf("FAIL: %s\n", m);                                                         \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

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
    for (int i = 0; i < 3; ++i)
    {
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(QColor(20 + i * 40, 30, 40));
        CHECK(image.save(gallery + QStringLiteral("/image%1.png").arg(i)),
              "selection fixture image is written");
    }

    ThumbnailPanel panel;
    SelectionModel selection;
    panel.setSelectionModel(&selection);
    panel.resize(640, 280);
    panel.show();
    panel.setDirectory(gallery);
    pumpUntil([&panel] { return panel.pathList().size() == 3; });
    CHECK(panel.pathList().size() == 3, "thumbnail panel lists all fixtures");

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

    std::printf("M36 browse and selection contract tests: %s\n",
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
