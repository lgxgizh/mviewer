#include "core/plugin/PluginLoader.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static std::string s_lastError;

std::string PluginLoader::lastError() { return s_lastError; }

std::vector<std::string>
PluginLoader::scanDirectory(const std::string &dirPath) {
  std::vector<std::string> candidates;
  std::filesystem::path dir(dirPath);
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    return candidates;

#ifdef _WIN32
  const std::string ext = ".dll";
#else
#ifdef __APPLE__
  const std::string ext = ".dylib";
#else
  const std::string ext = ".so";
#endif
#endif

  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ext)
      candidates.push_back(entry.path().string());
  }
  return candidates;
}

PluginLoader::LoadedPlugin PluginLoader::loadPlugin(const std::string &path) {
  LoadedPlugin result;
  result.path = path;

#ifdef _WIN32
  HMODULE handle = LoadLibraryA(path.c_str());
  if (!handle) {
    result.error = "LoadLibrary failed: " + std::to_string(GetLastError());
    s_lastError = result.error;
    return result;
  }

  auto createFn = reinterpret_cast<Analyzer *(*)()>(
      GetProcAddress(handle, "createAnalyzer"));
  auto nameFn =
      reinterpret_cast<const char *(*)()>(GetProcAddress(handle, "pluginName"));

  if (!createFn) {
    result.error = "createAnalyzer export not found";
    s_lastError = result.error;
    FreeLibrary(handle);
    return result;
  }
#else
  void *handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) {
    result.error = "dlopen failed: " + std::string(dlerror());
    s_lastError = result.error;
    return result;
  }

  auto createFn =
      reinterpret_cast<Analyzer *(*)()>(dlsym(handle, "createAnalyzer"));
  auto nameFn =
      reinterpret_cast<const char *(*)()>(dlsym(handle, "pluginName"));

  if (!createFn) {
    result.error = "createAnalyzer export not found";
    s_lastError = result.error;
    dlclose(handle);
    return result;
  }
#endif

  // Use the plugin name or fallback to filename
  if (nameFn) {
    result.name = nameFn();
  } else {
    result.name = std::filesystem::path(path).stem().string();
  }

  // Create instance and register
  Analyzer *analyzer = createFn();
  if (!analyzer) {
    result.error = "createAnalyzer returned null";
    s_lastError = result.error;
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
    return result;
  }

  // Register with the analyzer registry (wrap raw pointer factory into
  // unique_ptr)
  std::string id = analyzer->name();
  AnalyzerRegistry::instance().registerAnalyzer(
      id, [createFn]() -> std::unique_ptr<Analyzer> {
        return std::unique_ptr<Analyzer>(createFn());
      });

  result.loaded = true;
  std::cout << "[PluginLoader] Loaded plugin: " << result.name
            << " (analyzer: " << id << ") from " << path << std::endl;

// Note: handle is intentionally leaked for lifetime of the loaded library.
// In production, store handles in a plugin manager for proper cleanup.
#ifdef _WIN32
  // handle kept open
#else
  // handle kept open
#endif

  return result;
}

std::vector<PluginLoader::LoadedPlugin>
PluginLoader::loadFromDirectory(const std::string &dirPath) {
  std::vector<LoadedPlugin> results;
  auto candidates = scanDirectory(dirPath);
  for (const auto &path : candidates) {
    results.push_back(loadPlugin(path));
  }
  if (results.empty()) {
    std::cout << "[PluginLoader] No plugins found in " << dirPath << std::endl;
  }
  return results;
}
