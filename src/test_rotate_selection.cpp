// Browse rotate/flip must follow SelectionModel (ADR-012), not a stale Viewer path.
//
// Viewer closed → gallery selects B → rotate/flip QAction must rewrite B, not A.
// Compare rotate with no panes must not invent cell 0.

#include "compareworkspace.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "widgets/rawimageview.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <functional>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

void pump(int ms = 30)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

[[noreturn]] void terminateTestProcess(int exitCode)
{
#ifdef Q_OS_WIN
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exitCode));
    std::abort();
#else
    std::_Exit(exitCode);
#endif
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}

QString writeSizedPng(const QDir &dir, const QString &name, int w, int h, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

QString writeSplitPng(const QDir &dir, const QString &name, int w, int h)
{
    const QString path = dir.filePath(name);
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
            image.setPixelColor(x, y, x < w / 2 ? QColor(200, 0, 0) : QColor(0, 0, 200));
    }
    image.save(path, "PNG");
    return path;
}

ImageViewer *findViewer()
{
    for (QWidget *w : QApplication::topLevelWidgets())
    {
        if (auto *viewer = qobject_cast<ImageViewer *>(w))
            return viewer;
    }
    return nullptr;
}

RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (v && v->cellIndex() == index)
            return v;
    }
    return nullptr;
}

void dismissMessageBoxes()
{
    for (QWidget *w : QApplication::topLevelWidgets())
    {
        if (auto *box = qobject_cast<QMessageBox *>(w))
            box->accept();
    }
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-rotate-selection-test");
    QCoreApplication::setApplicationName("mviewer-rotate-selection-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    const QString appConfig =
        mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    QFile::remove(QDir(appConfig).filePath(QStringLiteral("recovery.json")));

    {
        ImageViewer viewer;
        QImage img(8, 4, QImage::Format_RGB32);
        img.fill(QColor(10, 20, 30));
        viewer.setProvisionalImage(QStringLiteral("/tmp/stale_a.png"), img);
        CHECK(viewer.currentPath().endsWith(QStringLiteral("stale_a.png")),
              "provisional open sets currentPath");
        viewer.close();
        CHECK(viewer.currentPath().isEmpty(), "closeEvent clears currentPath (no open file)");
        CHECK(!viewer.rotateCW(), "rotate on closed viewer is a no-op");
    }

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    QDir directory(temporary.filePath("browse"));
    directory.mkpath(".");
    const QString pathA = writeSizedPng(directory, "a_stale.png", 8, 4, QColor(200, 0, 0));
    const QString pathB = writeSizedPng(directory, "b_current.png", 6, 10, QColor(0, 0, 200));

    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pump(80);

    auto *panel = window.findChild<ThumbnailPanel *>();
    auto *selection = window.findChild<SelectionModel *>();
    auto *rotateCw = window.findChild<QAction *>(QStringLiteral("rotateCWAction"));
    CHECK(panel && selection && rotateCw, "MainWindow owns gallery, SSOT, rotate action");
    if (!panel || !selection || !rotateCw)
        terminateTestProcess(1);

    panel->setDirectory(directory.absolutePath());
    CHECK(waitFor(
              [&]
              {
                  return panel->currentDir().compare(directory.absolutePath(),
                                                     Qt::CaseInsensitive) == 0 &&
                         !panel->entries().isEmpty();
              }),
          "gallery published the two fixtures");

    panel->selectPath(pathA);
    CHECK(waitFor([&] { return selection->currentImage() == pathA; }),
          "SSOT current is A before Viewer open");

    window.onImageOpen(pathA);
    pump(80);
    ImageViewer *viewer = findViewer();
    CHECK(viewer != nullptr, "ImageViewer is a top-level window");
    if (viewer)
        CHECK(viewer->currentPath() == pathA, "Viewer opened A");

    if (viewer)
        viewer->close();
    pump(80);
    CHECK(!viewer || viewer->currentPath().isEmpty(), "closed Viewer has no open file");
    CHECK(!viewer || viewer->isHidden(), "Viewer is hidden after close");

    panel->selectPath(pathB);
    CHECK(waitFor([&] { return selection->currentImage() == pathB && rotateCw->isEnabled(); }),
          "SSOT current is B and rotate is enabled for B");

    QTimer dismiss;
    dismiss.setInterval(20);
    QObject::connect(&dismiss, &QTimer::timeout, &app, [] { dismissMessageBoxes(); });
    dismiss.start();
    rotateCw->trigger();
    pump(50);
    dismiss.stop();
    dismissMessageBoxes();

    QImage afterA(pathA);
    QImage afterB(pathB);
    CHECK(!afterA.isNull() && afterA.width() == 8 && afterA.height() == 4, "stale A is untouched");
    CHECK(!afterB.isNull() && afterB.width() == 10 && afterB.height() == 6,
          "rotate action rewrote gallery current B (6x10 -> 10x6)");

    {
        CompareWorkspace compare;
        CHECK(compare.editCellIndex() < 0, "empty Compare has no edit cell");
        compare.rotateCurrentCell(90);
        compare.flipCurrentCell(true);
        CHECK(compare.editCellIndex() < 0,
              "Compare transform without panes does not invent cell 0");
    }

    const QString pathC = writeSplitPng(directory, "c_flip.png", 8, 4);
    panel->setDirectory(directory.absolutePath());
    CHECK(waitFor([&] { return panel->pathList().contains(pathC); }),
          "gallery published flip fixture");
    panel->selectPath(pathC);
    CHECK(waitFor([&] { return selection->currentImage() == pathC; }),
          "SSOT current is C for flip");

    auto *flipH = window.findChild<QAction *>(QStringLiteral("flipHAction"));
    CHECK(flipH && flipH->isEnabled(), "flip action enabled for gallery current");
    if (flipH)
    {
        dismiss.start();
        flipH->trigger();
        pump(50);
        dismiss.stop();
        dismissMessageBoxes();
    }

    QImage afterC(pathC);
    CHECK(!afterC.isNull() && afterC.width() == 8 && afterC.height() == 4, "flip keeps C dims");
    CHECK(afterC.pixelColor(0, 0) == QColor(0, 0, 200), "H-flip writes left-from-right on C");
    CHECK(afterC.pixelColor(7, 0) == QColor(200, 0, 0), "H-flip writes right-from-left on C");
    QImage stillA(pathA);
    CHECK(stillA.width() == 8 && stillA.height() == 4, "flip leaves stale A untouched");

    // Multi-select batch rotate
    const QString pathD1 = writeSizedPng(directory, "d1_multi.png", 12, 6, QColor(50, 100, 150));
    const QString pathD2 = writeSizedPng(directory, "d2_multi.png", 16, 8, QColor(150, 100, 50));
    panel->setDirectory(directory.absolutePath());
    CHECK(waitFor(
              [&]
              { return panel->pathList().contains(pathD1) && panel->pathList().contains(pathD2); }),
          "gallery published multi-select fixtures");
    panel->selectPaths({pathD1, pathD2});
    CHECK(waitFor([&] { return selection->selection().size() == 2 && rotateCw->isEnabled(); }),
          "SelectionModel has 2 paths and rotate is enabled");
    dismiss.start();
    rotateCw->trigger();
    pump(50);
    dismiss.stop();
    dismissMessageBoxes();

    QImage afterD1(pathD1);
    QImage afterD2(pathD2);
    CHECK(!afterD1.isNull() && afterD1.width() == 6 && afterD1.height() == 12,
          "batch rotate rotated D1 (12x6 -> 6x12)");
    CHECK(!afterD2.isNull() && afterD2.width() == 8 && afterD2.height() == 16,
          "batch rotate rotated D2 (16x8 -> 8x16)");

    // Compare hover and explicit targeting
    {
        CompareWorkspace compare;
        compare.resize(600, 400);
        compare.setImages({pathD1, pathD2});
        CHECK(waitFor([&] { return compare.comparedImageCount() == 2; }),
              "Compare loaded two panes");
        pump(50);

        CHECK(compare.editCellIndex() < 0, "uninteracted Compare has no edit cell");
        compare.rotateCurrentCell(90);
        CHECK(compare.editCellIndex() < 0,
              "Compare transform without active pane does not invent cell 0");

        RawImageView *v1 = paneView(&compare, 1);
        CHECK(v1 != nullptr, "pane 1 view exists");
        if (v1)
        {
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(v1, &enter);
            compare.rotateCurrentCell(90);
            CHECK(compare.editCellIndex() == 1, "Compare transform targets hovered pane 1");
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(v1, &leave);
        }

        RawImageView *v0 = paneView(&compare, 0);
        CHECK(v0 != nullptr, "pane 0 view exists");
        if (v0)
        {
            // Retarget via Enter (same handleCellEvent path as hover). Synthetic
            // MouseButtonPress/Release is unreliable against unshown widgets in
            // offscreen CI, so prefer Enter after Leave cleared pane-1 hover.
            // Also fire a press so m_explicitEditIdx is set when the filter runs.
            const QPoint clickPos(10, 10);
            const QPointF globalPos(v0->mapToGlobal(clickPos));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(clickPos), globalPos,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v0, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(clickPos), globalPos,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v0, &release);
            QEvent enter0(QEvent::Enter);
            QApplication::sendEvent(v0, &enter0);
            pump(20);
            compare.rotateCurrentCell(90);
            CHECK(compare.editCellIndex() == 0,
                  "Compare transform retargets to pane 0 after leaving pane 1");
        }
    }

    if (g_failures)
    {
        std::cerr << g_failures << " failure(s)\n";
        terminateTestProcess(1);
    }
    std::cout << "All rotate-selection tests passed.\n";
    terminateTestProcess(0);
}
