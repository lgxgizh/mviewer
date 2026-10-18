#include "core/plugin/PluginLoader.h"
#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginABI.h"

#include <QDebug>

#include <iostream>
#include <memory>

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
using AbiFn = const PluginABI *(*)();
using VersionFn = int (*)();

void closePluginHandle(void *handle)
{
    if (!handle)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

template <typename Fn> Fn lookupPluginSymbol(void *handle, const char *name)
{
#ifdef _WIN32
    return reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<Fn>(dlsym(handle, name));
#endif
}

void logRejectedPlugin(const std::string &path, const std::string &reason)
{
    qWarning().nospace().noquote() << "[PluginLoader] Rejected: " << QString::fromUtf8(path.c_str())
                                   << ": " << QString::fromUtf8(reason.c_str());
}

// Extra loader reference held by analyzer factories and instance deleters.
// The original dlopen/LoadLibrary from loadPlugin is not closed on success
// (ADR-017). This pin is the reference that keeps the module mapped for as
// long as an Analyzer instance exists.
struct PluginModulePin
{
    void *handle = nullptr;
    PluginModulePin() = default;
    ~PluginModulePin()
    {
        closePluginHandle(handle);
    }
    PluginModulePin(const PluginModulePin &) = delete;
    PluginModulePin &operator=(const PluginModulePin &) = delete;
};

void *reopenPlugin(const std::string &path)
{
#ifdef _WIN32
    const std::filesystem::path nativePath = mviewer::core::pathFromUtf8(path);
    return reinterpret_cast<void *>(LoadLibraryW(nativePath.native().c_str()));
#else
    return dlopen(path.c_str(), RTLD_LAZY);
#endif
}

std::shared_ptr<PluginModulePin> pinPluginModule(const std::string &path)
{
    auto pin = std::make_shared<PluginModulePin>();
    pin->handle = reopenPlugin(path);
    if (!pin->handle)
        return nullptr;
    return pin;
}

AnalyzerDeleter makeAnalyzerDeleter(const std::shared_ptr<PluginModulePin> &module,
                                    DestroyAnalyzerFn destroyFn)
{
    return [module, destroyFn](Analyzer *value)
    {
        if (!module || module->handle == nullptr || destroyFn == nullptr)
            return;
        if (value)
            destroyFn(value);
    };
}

bool pluginAbiAccepted(void *handle, const std::string &path, std::string &error)
{
    const AbiFn abiFn = lookupPluginSymbol<AbiFn>(handle, "mviewer_plugin_abi");
    const VersionFn versionFn = lookupPluginSymbol<VersionFn>(handle, "mviewer_plugin_api_version");

    if (abiFn)
    {
        const PluginABI *pabi = abiFn();
        if (!pabi)
        {
            error = "plugin ABI descriptor is null";
            logRejectedPlugin(path, error);
            return false;
        }
        if (!pluginABICompatible(hostPluginABI(), *pabi))
        {
            error =
                "plugin ABI incompatible: host abi=" + std::to_string(hostPluginABI().abiVersion) +
                " api=" + std::to_string(hostPluginABI().apiVersion) +
                ", plugin abi=" + std::to_string(pabi->abiVersion) +
                " api=" + std::to_string(pabi->apiVersion);
            logRejectedPlugin(path, error);
            return false;
        }
        const std::string warn = pluginABIWarnings(hostPluginABI(), *pabi);
        if (!warn.empty())
            std::cout << "[PluginLoader] Warning: " << warn << '\n';
        return true;
    }

    // Legacy single-version export. Same gate on every platform; only the
    // version query itself is called before this decision.
    if (versionFn && versionFn() != MVIEWER_PLUGIN_API_VERSION)
    {
        error = "plugin API version mismatch (legacy export)";
        logRejectedPlugin(path, error);
        return false;
    }
    return true;
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
        result.name = mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(path).stem());
    }

    if (!destroyFn)
    {
        result.error = "destroyAnalyzer export not found";
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    const std::shared_ptr<PluginModulePin> module = pinPluginModule(path);
    if (!module)
    {
        result.error = "failed to pin plugin module for " + path;
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    // The probe used to read the analyzer id is destroyed through the plugin,
    // not leaked and not deleted on the host heap.
    std::unique_ptr<Analyzer, AnalyzerDeleter> probe(createFn(),
                                                     makeAnalyzerDeleter(module, destroyFn));
    if (!probe)
    {
        result.error = "createAnalyzer returned null";
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    const std::string id = probe->name();
    probe.reset();

    AnalyzerRegistry::instance().registerAnalyzer(
        id,
        [module, createFn, destroyFn]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
        {
            Analyzer *created = createFn();
            if (!created)
                return nullptr;
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                created, makeAnalyzerDeleter(module, destroyFn));
        });

    result.loaded = true;
    std::cout << "[PluginLoader] Loaded plugin: " << result.name << " (analyzer: " << id
              << ") from " << path << '\n';

    // The module handle is deliberately kept mapped for the process lifetime:
    // unloading a Qt-linking plugin DLL is unsafe on Windows (DLL-detach / CRT
    // static ordering), so PluginManager::unload() drops the registration but
    // never calls FreeLibrary/dlclose. Loading the same path twice reuses the
    // already-mapped handle. See docs/adr/017-process-lifetime-services.md.
    // `handle` is the original loader reference and is intentionally not closed
    // (ADR-017). The pin held by the registry keeps one more reference alive
    // for the analyzer instances.

    return result;
}
PluginLoader::LoadedPlugin PluginLoader::loadPlugin(const std::string &path)
{
    LoadedPlugin result;
    result.path = path;

    void *handle = nullptr;
#ifdef _WIN32
    const std::filesystem::path nativePath = mviewer::core::pathFromUtf8(path);
    handle = reinterpret_cast<void *>(LoadLibraryW(nativePath.native().c_str()));
    if (!handle)
    {
        result.error = "LoadLibrary failed: " + std::to_string(GetLastError());
        s_lastError = result.error;
        return result;
    }
#else
    handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle)
    {
        const char *reason = dlerror();
        result.error = "dlopen failed: " + std::string(reason ? reason : "unknown");
        s_lastError = result.error;
        return result;
    }
#endif

    // ABI / legacy version check before any symbol other than the version query.
    if (!pluginAbiAccepted(handle, path, result.error))
    {
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    auto createFn = lookupPluginSymbol<CreateAnalyzerFn>(handle, "createAnalyzer");
    auto nameFn = lookupPluginSymbol<PluginNameFn>(handle, "pluginName");
    auto destroyFn = lookupPluginSymbol<DestroyAnalyzerFn>(handle, "destroyAnalyzer");

    if (!createFn)
    {
        result.error = "createAnalyzer export not found";
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    return finishLoadedPlugin(path, handle, createFn, destroyFn, nameFn);
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
        std::cout << "[PluginLoader] No plugins found in " << dirPath << '\n';
    }
    return results;
}
