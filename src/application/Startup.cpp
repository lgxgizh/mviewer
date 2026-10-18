#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include <filesystem>
#include <system_error>

namespace
{

void doStartupPlugins()
{
    namespace fs = std::filesystem;
    // Resolve the plugin home next to the executable so launches via
    // shortcuts / the Start Menu work regardless of the working directory.
    std::error_code absoluteEc;
    const fs::path pluginDir =
        fs::absolute(mviewer::core::pathFromUtf8(
                         QCoreApplication::applicationDirPath().toUtf8().toStdString()),
                     absoluteEc) /
        mviewer::core::pathFromUtf8("plugins");

    // The release package intentionally ships without plugins; create the
    // plugin home on first run so users can drop third-party plugins in
    // later and the startup log stays informative instead of alarming.
    std::error_code ec;
    if (!fs::exists(pluginDir, ec) || !fs::is_directory(pluginDir, ec))
        fs::create_directories(pluginDir, ec);
    if (absoluteEc || ec || !fs::is_directory(pluginDir, ec))
    {
        // qWarning/qInfo, not std::cout: a Windows GUI-subsystem build has no console and
        // only the Qt message handler reaches the file logger.
        qWarning().noquote() << "[Startup] Plugin directory unavailable:"
                             << QString::fromStdString(mviewer::core::pathToUtf8(pluginDir))
                             << QStringLiteral("(%1)").arg(QString::fromStdString(
                                    (absoluteEc ? absoluteEc : ec).message()));
        return;
    }

    auto &mgr = PluginManager::instance();
    const std::string pluginPath = mviewer::core::pathToUtf8(pluginDir);
    int count = mgr.loadDirectory(pluginPath);

    if (count == 0)
    {
        qInfo().noquote() << "[Startup] No plugins loaded from"
                          << QString::fromStdString(pluginPath);
    }
    else
    {
        qInfo().noquote() << "[Startup] Loaded" << count << "plugin(s) from"
                          << QString::fromStdString(pluginPath);
    }
}

} // namespace

void startupPlugins()
{
    doStartupPlugins();
}
