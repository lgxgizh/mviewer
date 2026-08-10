#include "core/plugin/PluginManager.h"

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
    const fs::path pluginDir =
        fs::absolute(QCoreApplication::applicationDirPath().toStdString()) / "plugins";

    // The release package intentionally ships without plugins; create the
    // plugin home on first run so users can drop third-party plugins in
    // later and the startup log stays informative instead of alarming.
    std::error_code ec;
    if (!fs::exists(pluginDir) || !fs::is_directory(pluginDir))
        fs::create_directories(pluginDir, ec);
    if (ec || !fs::is_directory(pluginDir))
    {
        std::cout << "[Startup] Plugin directory unavailable: " << pluginDir << " (" << ec.message()
                  << ")" << std::endl;
        return;
    }

    auto &mgr = PluginManager::instance();
    int count = mgr.loadDirectory(pluginDir.string());

    if (count == 0)
    {
        std::cout << "[Startup] No plugins loaded from " << pluginDir << std::endl;
    }
    else
    {
        std::cout << "[Startup] Loaded " << count << " plugin(s) from " << pluginDir << std::endl;
    }
}

} // namespace

void startupPlugins()
{
    doStartupPlugins();
}
