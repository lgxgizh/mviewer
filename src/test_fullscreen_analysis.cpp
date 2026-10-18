// Fullscreen ImageViewer "分析" must leave the viewer's fullscreen path and
// show the main-window AnalysisPanel. The panel is a child of the shell, so a
// fullscreen viewer would otherwise cover it.

#include "analysispanel.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "runtime_storage.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>

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

bool viewerIsFullscreen(const ImageViewer *viewer)
{
    return viewer->property("mviewerFullscreenRequested").toBool() || viewer->isFullScreen() ||
           viewer->windowState().testFlag(Qt::WindowFullScreen);
}

ImageViewer *findViewer()
{
    for (QWidget *top : QApplication::topLevelWidgets())
    {
        if (auto *viewer = qobject_cast<ImageViewer *>(top))
            return viewer;
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-fullscreen-analysis-test");
    QCoreApplication::setApplicationName("mviewer-fullscreen-analysis-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    const QString appConfig =
        mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    QFile::remove(QDir(appConfig).filePath(QStringLiteral("recovery.json")));

    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pump(80);

    ImageViewer *viewer = findViewer();
    auto *panel = window.findChild<AnalysisPanel *>(QStringLiteral("analysisPanel"));
    CHECK(viewer != nullptr, "MainWindow owns a top-level ImageViewer");
    CHECK(panel != nullptr, "MainWindow owns the analysis panel");
    if (!viewer || !panel)
    {
        std::cout << "fullscreen_analysis_tests: FAIL (" << g_failures << " failures)\n";
        std::fflush(stdout);
        terminateTestProcess(1);
    }

    CHECK(!panel->isVisible(), "analysis panel starts hidden");
    viewer->setFullscreenRequested(true);
    pump(30);
    CHECK(viewerIsFullscreen(viewer), "viewer reports fullscreen before the analysis request");

    const bool invoked =
        QMetaObject::invokeMethod(viewer, "analysisRequested", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("histogram")));
    CHECK(invoked, "analysisRequested is delivered to MainWindow");
    pump(50);

    CHECK(!viewerIsFullscreen(viewer), "analysis request leaves viewer fullscreen");
    CHECK(panel->isVisible(), "analysis request shows the analysis panel");

    if (g_failures > 0)
    {
        std::cout << "fullscreen_analysis_tests: FAIL (" << g_failures << " failures)\n";
        std::fflush(stdout);
        terminateTestProcess(1);
    }
    std::cout << "fullscreen_analysis_tests: PASS\n";
    std::fflush(stdout);
    terminateTestProcess(0);
}
