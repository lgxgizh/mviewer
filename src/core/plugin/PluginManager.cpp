#include "core/plugin/PluginManager.h"

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

PluginManager &PluginManager::instance()
{
    static PluginManager inst;
    return inst;
}

PluginManager::~PluginManager()
{
    unloadAll();
}

std::string PluginManager::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

std::vector<std::string> PluginManager::scanDirectory(const std::string &dirPath)
{
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

    for (const auto &entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            candidates.push_back(entry.path().string());
    }
    return candidates;
}

bool PluginManager::load(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Already loaded?
    if (m_plugins.count(path))
    {
        m_lastError = "already loaded: " + path;
        return false;
    }

#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle)
    {
        m_lastError = "LoadLibrary failed: " + std::to_string(GetLastError());
        return false;
    }

    auto createFn = reinterpret_cast<Analyzer *(*)()>(GetProcAddress(handle, "createAnalyzer"));
    auto nameFn = reinterpret_cast<const char *(*)()>(GetProcAddress(handle, "pluginName"));
    auto destroyFn =
        reinterpret_cast<void (*)(Analyzer *)>(GetProcAddress(handle, "destroyAnalyzer"));
#else
    void *handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle)
    {
        m_lastError = "dlopen failed: " + std::string(dlerror());
        return false;
    }

    auto createFn = reinterpret_cast<Analyzer *(*)()>(dlsym(handle, "createAnalyzer"));
    auto nameFn = reinterpret_cast<const char *(*)()>(dlsym(handle, "pluginName"));
    auto destroyFn = reinterpret_cast<void (*)(Analyzer *)>(dlsym(handle, "destroyAnalyzer"));
#endif

    if (!createFn)
    {
        m_lastError = "createAnalyzer export not found in " + path;
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return false;
    }

    const std::string name = nameFn ? nameFn() : std::filesystem::path(path).stem().string();

    // Probe one instance to read its analyzer id (used as the registry key).
    // We own it via the host's `delete` (the registry's unique_ptr uses the
    // default deleter, so plugins must be deletable by the host under a shared
    // CRT). Free the probe immediately to avoid leaking it.
    Analyzer *analyzer = createFn();
    if (!analyzer)
    {
        m_lastError = "createAnalyzer returned null for " + path;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }
    const std::string analyzerId = analyzer->name();
    delete analyzer;

    // Register a factory whose deleter calls the plugin's destroyAnalyzer so
    // allocation AND deallocation stay inside the plugin module's heap. This
    // avoids cross-module free() (host delete on plugin-allocated memory),
    // which is UB and crashes on teardown. If destroyAnalyzer is absent we
    // fall back to the host's default delete (only safe with a shared CRT).
    AnalyzerRegistry::instance().registerAnalyzer(
        analyzerId,
        [createFn, destroyFn]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
        {
            Analyzer *a = createFn();
            if (!a)
                return nullptr;
            if (destroyFn)
                return std::unique_ptr<Analyzer, AnalyzerDeleter>(a,
                                                                  [destroyFn](Analyzer *p)
                                                                  {
                                                                      if (p)
                                                                          destroyFn(p);
                                                                  });
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(a, [](Analyzer *p) { delete p; });
        });

    PluginEntry entry;
    entry.path = path;
    entry.name = name;
    entry.analyzerId = analyzerId;
    entry.handle = handle;
    entry.loaded = true;
    m_plugins[path] = entry;

    std::cout << "[PluginManager] Loaded: " << name << " (analyzer: " << analyzerId << ") from "
              << path << std::endl;
    return true;
}

int PluginManager::loadDirectory(const std::string &dirPath)
{
    const auto candidates = scanDirectory(dirPath);
    int count = 0;
    for (const auto &path : candidates)
    {
        if (load(path))
            ++count;
    }
    return count;
}

bool PluginManager::unload(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_plugins.find(path);
    if (it == m_plugins.end())
        return false;

    // Drop the analyzer registration so no dangling factory (pointing into the
    // plugin module) remains in the registry.
    AnalyzerRegistry::instance().unregister(it->second.analyzerId);

    // NOTE: we intentionally do NOT FreeLibrary/dlclose here. Unloading a
    // Qt-linking plugin DLL at runtime (or during process teardown) is unsafe
    // on Windows — the OS DLL detach / CRT static ordering crashes the process.
    // Plugins are process-lifetime; the handle is reclaimed by the OS at exit.
    m_plugins.erase(it);

    std::cout << "[PluginManager] Released: " << path << std::endl;
    return true;
}

void PluginManager::unloadAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[path, entry] : m_plugins)
        AnalyzerRegistry::instance().unregister(entry.analyzerId);
    // Handles are intentionally NOT freed (see unload()). Plugins live for the
    // process lifetime; the OS reclaims them on exit.
    m_plugins.clear();
}

std::vector<PluginManager::PluginEntry> PluginManager::loadedPlugins() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginEntry> out;
    for (const auto &[path, entry] : m_plugins)
        out.push_back(entry);
    return out;
}

bool PluginManager::isLoaded(const std::string &path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins.count(path) > 0;
}

size_t PluginManager::count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins.size();
}
