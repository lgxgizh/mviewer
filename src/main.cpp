#include "mainwindow.h"

#include "application/Startup.h"
#include "core/CrashHandler.h"
#include "core/SelfTest.h"

#include <QApplication>

#include <string>

class MainWindow;
static QString g_openOnLaunch;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    // P5: crash diagnostics (opt-in via MVIEWER_CRASH_DUMP=1).
    mviewer::core::installCrashHandler("MViewer");

    // P5: headless release self-test gate. Runs before any window is created so
    // a release pipeline can verify the decode path without a display.
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--selftest")
            return mviewer::core::runSelfTest();
    }

    // M14-1: Windows Native — open a file directly from the command line.
    // `mviewer.exe image.jpg` → open the image instead of an empty window.
    for (int i = 1; i < argc; ++i)
    {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith("-") && !arg.startsWith("/"))
        {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
            {
                g_openOnLaunch = fi.absoluteFilePath();
                break;
            }
        }
    }

    // Load plugins (if any)
    startupPlugins();

    MainWindow mainWindow;
    if (!g_openOnLaunch.isEmpty())
        mainWindow.setOpenOnLaunch(g_openOnLaunch);
    mainWindow.show();

    return app.exec();
}
