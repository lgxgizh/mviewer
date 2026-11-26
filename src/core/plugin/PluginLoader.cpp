#include "core/plugin/PluginLoader.h"
#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginABI.h"
#include "core/plugin/PluginAbiCall.h"
#include "core/plugin/PluginLibrary.h"
#include "core/plugin/PluginPathPolicy.h"

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
    return mviewer::core::scanPluginCandidates(dirPath);
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
    std::string ignored;
    return mviewer::core::loadPluginLibrary(path, ignored);
}

std::shared_ptr<PluginModulePin> pinPluginModule(const std::string &path, void *loadedHandle)
{
    auto pin = std::make_shared<PluginModulePin>();
#ifdef _WIN32
    // Prefer an address inside the already-mapped module: reopening by path is a
    // second window in which the file could be swapped for something else.
    if (loadedHandle != nullptr)
    {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                reinterpret_cast<LPCWSTR>(loadedHandle), &mod))
            return nullptr;
        pin->handle = reinterpret_cast<void *>(mod);
        return pin;
    }
#endif
    std::string ignored;
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
        if (!value)
            return;
        std::string ignored;
        mviewer::core::invokePluginVoid("destroyAnalyzer", ignored, [&] { destroyFn(value); });
    };
}

bool pluginAbiAccepted(void *handle, const std::string &path, std::string &error)
{
    const AbiFn abiFn = lookupPluginSymbol<AbiFn>(handle, "mviewer_plugin_abi");
    const VersionFn versionFn = lookupPluginSymbol<VersionFn>(handle, "mviewer_plugin_api_version");

    if (abiFn)
    {
        bool abiOk = false;
        const PluginABI *pabi = mviewer::core::invokePluginCallback("mviewer_plugin_abi", error,
                                                                    abiOk, [&] { return abiFn(); });
        if (!abiOk)
        {
            logRejectedPlugin(path, error);
            return false;
        }
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

    // A module with no contract export is rejected. Mapping it already ran
    // DllMain or .init_array; that is not a trust check. No later plugin entry
    // point is resolved or called.
    if (!versionFn)
    {
        error = mviewer::core::kPluginMissingAbiError;
        logRejectedPlugin(path, error);
        return false;
    }
    // Legacy single-version export. Same gate on every platform; only the
    // version query itself is called before this decision.
    bool versionOk = false;
    const int version = mviewer::core::invokePluginCallback("mviewer_plugin_api_version", error,
                                                            versionOk, [&] { return versionFn(); });
    if (!versionOk)
    {
        logRejectedPlugin(path, error);
        return false;
    }
    if (version != MVIEWER_PLUGIN_API_VERSION)
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
    // Use the plugin name or fallback to filename. A null pluginName is not
    // a name; constructing a std::string from it would be undefined.
    if (nameFn)
    {
        bool nameOk = false;
        const char *raw = mviewer::core::invokePluginCallback("pluginName", result.error, nameOk,
                                                              [&] { return nameFn(); });
        if (!nameOk)
        {
            s_lastError = result.error;
            closePluginHandle(handle);
            return result;
        }
        if (!raw)
        {
            result.error = "pluginName returned null";
            s_lastError = result.error;
            closePluginHandle(handle);
            return result;
        }
        result.name = raw;
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

    const std::shared_ptr<PluginModulePin> module = pinPluginModule(path, handle);
    if (!module)
    {
        result.error = "failed to pin plugin module for " + path;
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    // The probe used to read the analyzer id is destroyed through the plugin,
    // not leaked and not deleted on the host heap. The catch stops a C++
    // exception only. It is not a sandbox; the plugin runs in this process.
    bool createOk = false;
    Analyzer *created = mviewer::core::invokePluginCallback("createAnalyzer", result.error,
                                                            createOk, [&] { return createFn(); });
    if (!createOk)
    {
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }
    std::unique_ptr<Analyzer, AnalyzerDeleter> probe(created,
                                                     makeAnalyzerDeleter(module, destroyFn));
    if (!probe)
    {
        result.error = "createAnalyzer returned null";
        s_lastError = result.error;
        closePluginHandle(handle);
        return result;
    }

    bool nameOk = false;
    const std::string id = mviewer::core::invokePluginCallback(
        "Analyzer::name", result.error, nameOk, [&] { return probe->name(); });
    if (!nameOk)
    {
        s_lastError = result.error;
        probe.reset();
        closePluginHandle(handle);
        return result;
    }
    probe.reset();

    AnalyzerRegistry::instance().registerAnalyzer(
        id,
        [module, createFn, destroyFn]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
        {
            // C++ catch only. Not a sandbox: the plugin is in this process.
            std::string ignored;
            bool ok = false;
            Analyzer *created = mviewer::core::invokePluginCallback("createAnalyzer", ignored, ok,
                                                                    [&] { return createFn(); });
            if (!ok || !created)
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

    const mviewer::core::PluginFileAssessment file = mviewer::core::assessPluginFile(path, {});
    if (!file.regularFile)
    {
        result.error = file.error.empty() ? "plugin path is not a regular file" : file.error;
        s_lastError = result.error;
        return result;
    }

    void *handle = nullptr;
    handle = mviewer::core::loadPluginLibrary(path, result.error);
    if (!handle)
    {
        s_lastError = result.error;
        return result;
    }

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
std::vector<PluginLoader::LoadedPlugin> PluginLoader::loadFromDirectory(const std::string &)
{
    // Directory discovery must not map unknown modules. loadPlugin is the
    // explicit entry for one regular file the caller named.
    s_lastError = "refusing to map untrusted modules from a directory";
    return {};
}
