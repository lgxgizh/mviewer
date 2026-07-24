#pragma once
#include "core/analyzer/Analyzer.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations for platform-specific handle types
#ifdef _WIN32
using PluginHandle = void *; // HMODULE
#else
using PluginHandle = void *; // void* from dlopen
#endif

// PluginManager: persistent handle storage + lifecycle management (RFC-013).
// Owns all loaded plugin handles; auto-unloads on destruction.
class PluginManager
{
  public:
    static PluginManager &instance();

    struct PluginEntry
    {
        std::string path;
        std::string name;
        std::string analyzerId;
        std::string decoderId;
        std::string exporterId;
        std::string importerId; // A-9.3
        PluginHandle handle = nullptr;
        bool loaded = false;
    };

    // Load a plugin from path; stores handle for lifetime management.
    bool load(const std::string &path);

    // Load all plugins from a directory.
    int loadDirectory(const std::string &dirPath);

    // Unload a specific plugin by path.
    bool unload(const std::string &path);

    // Unload all plugins.
    void unloadAll();

    // Query loaded plugins.
    std::vector<PluginEntry> loadedPlugins() const;
    bool isLoaded(const std::string &path) const;
    size_t count() const;

    // Scan directory for candidate plugin files.
    static std::vector<std::string> scanDirectory(const std::string &dirPath);

    // Get last error.
    std::string lastError() const;

  private:
    PluginManager() = default;
    ~PluginManager();
    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, PluginEntry> m_plugins;
    std::string m_lastError;
};
