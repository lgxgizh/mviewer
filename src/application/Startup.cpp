#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginManager.h"
#include "core/plugin/PluginPathPolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QString>

#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

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
    std::string canonicalDir;
    std::string dirError;
    // A symlink or junction in place of plugins/ is not a real directory.
    // Refuse it before any module is mapped.
    if (!mviewer::core::assessPluginDirectory(pluginPath, canonicalDir, dirError))
    {
        qWarning().noquote() << "[Startup] Plugin directory unavailable:"
                             << QString::fromStdString(pluginPath)
                             << QString::fromStdString(dirError);
        return;
    }
    // Startup is not consent. Only hashes already stored by Trust and load
    // are mapped. A newly dropped library is not executed here.
    // Built-in analyzers are registered in-process and do not use this path.
    QSettings settings;
    const QStringList trusted =
        settings.value(QStringLiteral("plugins/trustedSha256")).toStringList();
    std::vector<std::string> hashes;
    hashes.reserve(static_cast<size_t>(trusted.size()));
    for (const QString &item : trusted)
        hashes.emplace_back(item.toUtf8().toStdString());

    // "Enable / Disable" stores canonical file paths; a disabled plugin must not
    // even be hashed, let alone mapped.
    std::set<std::string> disabled;
    const QStringList disabledRaw = settings.value(QStringLiteral("plugins/disabled")).toStringList();
    for (const QString &item : disabledRaw)
    {
        const mviewer::core::PluginFileAssessment assessed =
            mviewer::core::assessPluginFile(item.toUtf8().toStdString(), {});
        if (assessed.regularFile && !assessed.canonicalUtf8.empty())
            disabled.insert(assessed.canonicalUtf8);
    }

    const int count = mgr.loadTrusted(pluginPath, hashes, disabled);

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
