#include "mainwindow.h"

#include "MViewerVersion.h"
#include "application/CommandLine.h"
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
static QStringList g_openOnLaunch;

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

    // M51: collect positional arguments first. Classification happens after
    // plugins are loaded so plugin-provided image suffixes use the same
    // contract as shell associations and drag-and-drop.
    QStringList positionalTargets;
    for (int i = 1; i < arguments.size(); ++i)
    {
        const QString &arg = arguments.at(i);
        if (mviewer::application::isPositionalOpenArgument(arg))
            positionalTargets.append(arg);
    }

    // Load plugins (if any)
    startupPlugins();

    if (!positionalTargets.isEmpty())
    {
        const auto plan = mviewer::application::planExternalOpen(positionalTargets);
        if (plan.isValid())
            g_openOnLaunch = plan.paths;
        else
            qWarning().noquote() << plan.error;
    }

    MainWindow mainWindow;
    if (!g_openOnLaunch.isEmpty())
        mainWindow.setOpenOnLaunch(g_openOnLaunch);
    mainWindow.show();

    return app.exec();
}
