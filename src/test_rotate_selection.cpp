// Browse rotate must follow SelectionModel (ADR-012), not a stale Viewer path.
//
// Viewer closed → gallery selects B → rotate QAction must rewrite B, not A.

#include "imageviewer.h"
#include "mainwindow.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QMessageBox>
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

ImageViewer *findViewer()
{
    for (QWidget *w : QApplication::topLevelWidgets())
    {
        if (auto *viewer = qobject_cast<ImageViewer *>(w))
            return viewer;
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

    if (g_failures)
    {
        std::cerr << g_failures << " failure(s)\n";
        terminateTestProcess(1);
    }
    std::cout << "All rotate-selection tests passed.\n";
    terminateTestProcess(0);
}
