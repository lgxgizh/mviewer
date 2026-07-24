#include "mainwindow.h"

#include "application/Startup.h"
#include "core/CrashHandler.h"
#include "core/Logger.h"
#include "core/SelfTest.h"
#include "core/SettingsIO.h"

#include <QApplication>

#include <string>

class MainWindow;
static QString g_openOnLaunch;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    // Structured file logging (AppData/logs/mviewer-YYYYMMDD.log).
    mviewer::core::installFileLogger("MViewer");

    // Crash diagnostics — always on; dumps land in AppData/crash-reports/.
    mviewer::core::installCrashHandler("MViewer");

    // Settings schema migration (idempotent, runs once per schema bump).
    mviewer::core::migrateSettingsIfNeeded();

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
