#include "core/plugin/PluginManager.h"

#include <filesystem>
#include <iostream>

namespace {

void doStartupPlugins() {
    namespace fs = std::filesystem;
    const fs::path pluginDir = fs::absolute("plugins");

    if (!fs::exists(pluginDir) || !fs::is_directory(pluginDir)) {
        std::cout << "[Startup] Plugin directory not found: " << pluginDir << std::endl;
        return;
    }

    auto& mgr = PluginManager::instance();
    int count = mgr.loadDirectory(pluginDir.string());

    if (count == 0) {
        std::cout << "[Startup] No plugins loaded from " << pluginDir << std::endl;
    } else {
        std::cout << "[Startup] Loaded " << count << " plugin(s) from " << pluginDir << std::endl;
    }
}

} // namespace

void startupPlugins() {
    doStartupPlugins();
}
