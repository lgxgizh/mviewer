#include "core/plugin/PluginManager.h"
#include "core/filesystem/Utf8Path.h"

#include <QCoreApplication>

#include <filesystem>
#include <iostream>
#include <system_error>

namespace
{

void doStartupPlugins()
{
    namespace fs = std::filesystem;
    // Resolve the plugin home next to the executable so launches via
    // shortcuts / the Start Menu work regardless of the working directory.
    std::error_code absoluteEc;
    const fs::path pluginDir = fs::absolute(
        mviewer::core::pathFromUtf8(QCoreApplication::applicationDirPath().toUtf8().toStdString()),
        absoluteEc) / mviewer::core::pathFromUtf8("plugins");

    // The release package intentionally ships without plugins; create the
    // plugin home on first run so users can drop third-party plugins in
    // later and the startup log stays informative instead of alarming.
    std::error_code ec;
    if (!fs::exists(pluginDir, ec) || !fs::is_directory(pluginDir, ec))
        fs::create_directories(pluginDir, ec);
    if (absoluteEc || ec || !fs::is_directory(pluginDir, ec))
    {
        std::cout << "[Startup] Plugin directory unavailable: " << mviewer::core::pathToUtf8(pluginDir)
                  << " (" << (absoluteEc ? absoluteEc : ec).message()
                  << ")" << std::endl;
        return;
    }

    auto &mgr = PluginManager::instance();
    const std::string pluginPath = mviewer::core::pathToUtf8(pluginDir);
    int count = mgr.loadDirectory(pluginPath);

    if (count == 0)
    {
        std::cout << "[Startup] No plugins loaded from " << pluginPath << std::endl;
    }
    else
    {
        std::cout << "[Startup] Loaded " << count << " plugin(s) from " << pluginPath << std::endl;
    }
}

} // namespace

void startupPlugins()
{
    doStartupPlugins();
}
