// Plugin loader tests (tests plugin loadFromDirectory + scanDirectory on empty
// dir)
#include "core/plugin/PluginLoader.h"

#include <cassert>
#include <iostream>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

int main()
{
    std::cout << "[PluginLoader tests]\n";

    // Empty directory scan returns empty vector
    auto empty = PluginLoader::scanDirectory("./nonexistent_dir");
    CHECK(empty.empty(), "scanDirectory(nonexistent) returns empty");

    // Load from nonexistent directory returns empty
    auto loaded = PluginLoader::loadFromDirectory("./nonexistent_dir");
    CHECK(loaded.empty(), "loadFromDirectory(nonexistent) returns empty");

    // Load invalid path reports error
    auto bad = PluginLoader::loadPlugin("./no_such_plugin.dll");
    CHECK(!bad.loaded, "loadPlugin(invalid) does not load");
    CHECK(!bad.error.empty(), "loadPlugin(invalid) sets error message");

    // Empty path
    auto emptyPath = PluginLoader::loadPlugin("");
    CHECK(!emptyPath.loaded, "loadPlugin(empty) does not load");

    std::cout << "PluginLoader tests done\n";
    return 0;
}
