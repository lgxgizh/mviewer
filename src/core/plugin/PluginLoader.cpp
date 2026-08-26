#include "core/plugin/PluginLoader.h"
#include "core/plugin/PluginABI.h"
#include "core/filesystem/Utf8Path.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static std::string s_lastError;

std::string PluginLoader::lastError()
{
    return s_lastError;
}

std::vector<std::string> PluginLoader::scanDirectory(const std::string &dirPath)
{
    std::vector<std::string> candidates;
    const std::filesystem::path dir = mviewer::core::pathFromUtf8(dirPath);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
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

    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc &&
            mviewer::core::pathToUtf8(it->path().extension()) == ext)
            candidates.push_back(mviewer::core::pathToUtf8(it->path()));
    }
    return candidates;
}

using CreateAnalyzerFn = Analyzer *(*)();
using DestroyAnalyzerFn = void (*)(Analyzer *);
using PluginNameFn = const char *(*)();

void closePluginHandle(void *handle)
{
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

PluginLoader::LoadedPlugin finishLoadedPlugin(const std::string &path, void *handle,
                                              CreateAnalyzerFn createFn,
                                              DestroyAnalyzerFn destroyFn, PluginNameFn nameFn)
{
    PluginLoader::LoadedPlugin result;
    result.path = path;
    // Use the plugin name or fallback to filename
    if (nameFn)
    {
        result.name = nameFn();
    }
    else
    {
        result.name = mviewer::core::pathToUtf8(
            mviewer::core::pathFromUtf8(path).stem());
    }

    // Create instance and register
    Analyzer *analyzer = createFn();
    if (!analyzer)
    {
        result.error = "createAnalyzer returned null";
        s_lastError = result.error;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return result;
    }

    // Register with the analyzer registry using the plugin's destroyAnalyzer
    // as the deleter so allocation AND deallocation stay in the plugin's heap.
    std::string id = analyzer->name();
    AnalyzerRegistry::instance().registerAnalyzer(
        id,
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

    result.loaded = true;
    std::cout << "[PluginLoader] Loaded plugin: " << result.name << " (analyzer: " << id
              << ") from " << path << std::endl;

// Note: handle is intentionally leaked for lifetime of the loaded library.
// In production, store handles in a plugin manager for proper cleanup.
#ifdef _WIN32
    // handle kept open
#else
    // handle kept open
#endif

    return result;
}
PluginLoader::LoadedPlugin PluginLoader::loadPlugin(const std::string &path)
{
    LoadedPlugin result;
    result.path = path;

#ifdef _WIN32
    const std::filesystem::path nativePath = mviewer::core::pathFromUtf8(path);
    HMODULE handle = LoadLibraryW(nativePath.native().c_str());
    if (!handle)
    {
        result.error = "LoadLibrary failed: " + std::to_string(GetLastError());
        s_lastError = result.error;
        return result;
    }

    auto createFn = reinterpret_cast<Analyzer *(*)()>(GetProcAddress(handle, "createAnalyzer"));
    auto nameFn = reinterpret_cast<const char *(*)()>(GetProcAddress(handle, "pluginName"));
    auto destroyFn =
        reinterpret_cast<void (*)(Analyzer *)>(GetProcAddress(handle, "destroyAnalyzer"));
    auto versionFn =
        reinterpret_cast<int (*)()>(GetProcAddress(handle, "mviewer_plugin_api_version"));

    if (!createFn)
    {
        result.error = "createAnalyzer export not found";
        s_lastError = result.error;
        FreeLibrary(handle);
        return result;
    }

    // M14.2: ABI triple check. Prefer the frozen descriptor; fall back to the
    // legacy single-version export for plugins that haven't adopted it yet.
    auto abiFn =
        reinterpret_cast<const PluginABI *(*)()>(GetProcAddress(handle, "mviewer_plugin_abi"));
    if (abiFn)
    {
        const PluginABI *pabi = abiFn();
        if (!pluginABICompatible(hostPluginABI(), *pabi))
        {
            result.error =
                "plugin ABI incompatible: host abi=" + std::to_string(hostPluginABI().abiVersion) +
                " api=" + std::to_string(hostPluginABI().apiVersion) +
                ", plugin abi=" + std::to_string(pabi->abiVersion) +
                " api=" + std::to_string(pabi->apiVersion);
            s_lastError = result.error;
            return result;
        }
        std::string warn = pluginABIWarnings(hostPluginABI(), *pabi);
        if (!warn.empty())
            std::cout << "[PluginLoader] Warning: " << warn << std::endl;
    }
    else if (versionFn && versionFn() != MVIEWER_PLUGIN_API_VERSION)
    {
        result.error = "plugin API version mismatch (legacy export)";
        s_lastError = result.error;
        FreeLibrary(handle);
        return result;
    }
#else
    void *handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle)
    {
        result.error = "dlopen failed: " + std::string(dlerror());
        s_lastError = result.error;
        return result;
    }

    auto createFn = reinterpret_cast<Analyzer *(*)()>(dlsym(handle, "createAnalyzer"));
    auto nameFn = reinterpret_cast<const char *(*)()>(dlsym(handle, "pluginName"));
    auto destroyFn = reinterpret_cast<void (*)(Analyzer *)>(dlsym(handle, "destroyAnalyzer"));

    if (!createFn)
    {
        result.error = "createAnalyzer export not found";
        s_lastError = result.error;
        dlclose(handle);
        return result;
    }

    // M14.2: ABI triple gate (mirrors the Windows branch).
    auto abiFn = reinterpret_cast<const PluginABI *(*)()>(dlsym(handle, "mviewer_plugin_abi"));
    if (abiFn)
    {
        const PluginABI *pabi = abiFn();
        if (!pluginABICompatible(hostPluginABI(), *pabi))
        {
            result.error =
                "plugin ABI incompatible: host abi=" + std::to_string(hostPluginABI().abiVersion) +
                " api=" + std::to_string(hostPluginABI().apiVersion) +
                ", plugin abi=" + std::to_string(pabi->abiVersion) +
                " api=" + std::to_string(pabi->apiVersion);
            s_lastError = result.error;
            return result;
        }
        std::string warn = pluginABIWarnings(hostPluginABI(), *pabi);
        if (!warn.empty())
            std::cout << "[PluginLoader] Warning: " << warn << std::endl;
    }
#endif

#ifdef _WIN32
    return finishLoadedPlugin(path, reinterpret_cast<void *>(handle), createFn, destroyFn, nameFn);
#else
    return finishLoadedPlugin(path, handle, createFn, destroyFn, nameFn);
#endif
}
std::vector<PluginLoader::LoadedPlugin> PluginLoader::loadFromDirectory(const std::string &dirPath)
{
    std::vector<LoadedPlugin> results;
    auto candidates = scanDirectory(dirPath);
    for (const auto &path : candidates)
    {
        results.push_back(loadPlugin(path));
    }
    if (results.empty())
    {
        std::cout << "[PluginLoader] No plugins found in " << dirPath << std::endl;
    }
    return results;
}
