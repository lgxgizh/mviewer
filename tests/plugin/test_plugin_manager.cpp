// PluginManager tests: persistent handles + lifecycle
#include "core/plugin/PluginManager.h"

#include <iostream>

#define CHECK(cond, msg)                               \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            std::cerr << "FAIL: " << msg << std::endl; \
            return 1;                                  \
        }                                              \
        else                                           \
        {                                              \
            std::cout << "PASS: " << msg << std::endl; \
        }                                              \
    } while (0)

int main()
{
    std::cout << "[PluginManager tests]\n";
    auto& mgr = PluginManager::instance();

    // Initially empty
    CHECK(mgr.count() == 0, "initially empty");
    CHECK(mgr.loadedPlugins().empty(), "no loaded plugins");

    // Scan nonexistent dir
    auto candidates = PluginManager::scanDirectory("./nonexistent");
    CHECK(candidates.empty(), "scan nonexistent returns empty");

    // Load nonexistent file fails gracefully
    bool ok = mgr.load("./no_such_plugin.dll");
    CHECK(!ok, "load nonexistent fails");
    CHECK(!mgr.lastError().empty(), "error message set");
    CHECK(mgr.count() == 0, "count still 0 after failed load");

    // Load directory with no plugins
    int n = mgr.loadDirectory("./nonexistent");
    CHECK(n == 0, "loadDirectory nonexistent returns 0");

    // isLoaded on nonexistent
    CHECK(!mgr.isLoaded("./foo.dll"), "isLoaded false for missing");

    // Unload nonexistent is no-op
    CHECK(!mgr.unload("./foo.dll"), "unload nonexistent returns false");

    std::cout << "PluginManager tests done\n";
    return 0;
}
