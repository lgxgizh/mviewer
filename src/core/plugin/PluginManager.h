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

namespace mviewer::core
{
class ICompareAlgorithm;
}

// One file the user identified in a trust confirmation, plus the SHA-256 they saw.
struct ExplicitPluginTrust
{
    std::string path;
    std::string sha256Hex;
};

// `loaded` is true only when this call mapped the file. `persist` is true only
// when that map succeeded and the bytes still match `sha256Hex`.
struct ExplicitPluginTrustResult
{
    std::string path;
    std::string sha256Hex;
    bool loaded = false;
    bool persist = false;
};

// Text for the trust dialog. Identifies every path and SHA-256 and states that
// the native code runs in this process.
inline std::string pluginTrustPromptBody(const std::vector<ExplicitPluginTrust> &candidates)
{
    std::string text = "Native code from these files executes inside this process and can "
                       "crash it. This is not a sandbox.\n";
    for (const ExplicitPluginTrust &item : candidates)
    {
        text += item.path;
        text += "\nSHA-256: ";
        text += item.sha256Hex;
        text += '\n';
    }
    return text;
}

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
        std::string importerId;
        std::string compareAlgorithmId; // Compare Algorithm plugin
        PluginHandle handle = nullptr;
        bool loaded = false;
    };

    // Map one regular file the caller named. Symlinks and junctions are
    // rejected before the module is mapped. This does not write QSettings and
    // is not a consent prompt.
    bool load(const std::string &path);

    // Discovery does not map modules. Returns 0.
    int loadDirectory(const std::string &dirPath);

    // Map only direct regular files in dirPath whose lowercase SHA-256 hex is
    // listed. Other candidates are not opened. Hashing reads bytes only.
    int loadTrusted(const std::string &dirPath, const std::vector<std::string> &trustedSha256Hex);

    // Map only when `accepted` is true. A false value loads nothing. An already
    // loaded path is not mapped again and is not given a new hash. The hash is
    // stored by the caller only when `persist` is true.
    std::vector<ExplicitPluginTrustResult>
    commitExplicitTrust(const std::vector<ExplicitPluginTrust> &confirmed, bool accepted);

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
