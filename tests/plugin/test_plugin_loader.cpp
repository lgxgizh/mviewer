// Plugin loader tests.
//
// NOTE: These tests only verify the failure paths (nonexistent directory,
// invalid DLL, empty path).  A proper integration test that loads a real
// plugin would require either:
//   1. A minimal test plugin built and deployed to a known directory
//   2. A mock DLL with the expected export ABI
// The plugins/example/ directory contains a sample plugin that can be
// used for this purpose when its build is integrated into the test
// harness.  Until then, manual verification is needed:
//   mviewer --plugindir plugins/example/build

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
