#pragma once
#include "core/analyzer/Analyzer.h"

#include <filesystem>
#include <string>
#include <vector>

// Plugin loading framework (RFC-013).
// Loads Analyzer plugins from shared libraries (.dll/.so/.dylib).
// Loaded plugins self-register with the AnalyzerRegistry at load time.
class PluginLoader
{
  public:
    struct LoadedPlugin
    {
        std::string path;
        std::string name;
        bool loaded = false;
        std::string error;
    };

    // Does not map modules. Unknown libraries are not executed by a directory
    // scan. Call loadPlugin for one explicitly named regular file.
    static std::vector<LoadedPlugin> loadFromDirectory(const std::string &dirPath);

    // Explicit load of one regular file. Symlinks and junctions are rejected
    // before the module is mapped.
    static LoadedPlugin loadPlugin(const std::string &path);

    // Scan directory and return list of candidate plugin files.
    static std::vector<std::string> scanDirectory(const std::string &dirPath);

    // Get last error (for diagnostics).
    static std::string lastError();
};
