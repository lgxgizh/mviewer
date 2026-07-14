#pragma once
#include "core/analyzer/Analyzer.h"
#include <filesystem>
#include <string>
#include <vector>

// Plugin loading framework (RFC-013).
// Loads Analyzer plugins from shared libraries (.dll/.so/.dylib).
// Loaded plugins self-register with the AnalyzerRegistry at load time.
class PluginLoader {
public:
  struct LoadedPlugin {
    std::string path;
    std::string name;
    bool loaded = false;
    std::string error;
  };

  // Load all plugins from a directory. Each plugin must export:
  //   extern "C" Analyzer* createAnalyzer();
  //   extern "C" void destroyAnalyzer(Analyzer*);
  //   extern "C" const char* pluginName();
  static std::vector<LoadedPlugin>
  loadFromDirectory(const std::string &dirPath);

  // Load a single plugin by path.
  static LoadedPlugin loadPlugin(const std::string &path);

  // Scan directory and return list of candidate plugin files.
  static std::vector<std::string> scanDirectory(const std::string &dirPath);

  // Get last error (for diagnostics).
  static std::string lastError();
};
