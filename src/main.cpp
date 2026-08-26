#include "mainwindow.h"

#include "MViewerVersion.h"
#include "application/Startup.h"
#include "core/CrashHandler.h"
#include "core/Logger.h"
#include "core/SelfTest.h"
#include "core/SettingsIO.h"
#include "runtime_storage.h"

#include <QApplication>
#include <QDebug>

#include <exception>
#include <string>

class MainWindow;
static QString g_openOnLaunch;

class MViewerApplication final : public QApplication
{
  public:
    using QApplication::QApplication;

    bool notify(QObject *receiver, QEvent *event) override
    {
        try
        {
            return QApplication::notify(receiver, event);
        }
        catch (const std::exception &error)
        {
            qCritical("Unhandled exception in Qt event handler (receiver=%s type=%d): %s",
                      receiver && receiver->metaObject() ? receiver->metaObject()->className()
                                                         : "<null>",
                      event ? static_cast<int>(event->type()) : -1, error.what());
        }
        catch (...)
        {
            qCritical("Unhandled unknown exception in Qt event handler (receiver=%s type=%d)",
                      receiver && receiver->metaObject() ? receiver->metaObject()->className()
                                                         : "<null>",
                      event ? static_cast<int>(event->type()) : -1);
        }
        return false;
    }
};

int main(int argc, char *argv[])
{
    MViewerApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");
    app.setApplicationVersion(QStringLiteral(MVIEWER_VERSION_STRING));
    mviewer::runtime::configureSettings();

    // Structured file logging (AppData/logs/mviewer-YYYYMMDD.log).
    mviewer::core::installFileLogger("MViewer");

    // Crash diagnostics — always on; dumps land in AppData/crash-reports/.
    mviewer::core::installCrashHandler("MViewer");

    // Settings schema migration (idempotent, runs once per schema bump).
    mviewer::core::migrateSettingsIfNeeded();

    // P5: headless release self-test gate. Runs before any window is created so
    // a release pipeline can verify the decode path without a display.
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i)
    {
        if (arguments.at(i) == QStringLiteral("--selftest"))
            return mviewer::core::runSelfTest();
    }

    // M14-1: Windows Native — open a file directly from the command line.
    // `mviewer.exe image.jpg` → open the image instead of an empty window.
    for (int i = 1; i < arguments.size(); ++i)
    {
        const QString &arg = arguments.at(i);
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
