#include "core/plugin/PluginManager.h"
#include "core/compare/ICompareAlgorithm.h"
#include "core/export/ExporterRegistry.h"
#include "core/export/IExporter.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/import/IImporter.h"
#include "core/import/ImporterRegistry.h"
#include "core/plugin/PluginABI.h"
#include "core/plugin/PluginAbiCall.h"
#include "core/plugin/PluginLibrary.h"
#include "core/plugin/PluginPathPolicy.h"

#include <QDebug>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

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
    std::lock_guard<std::mutex> lock(m_mutex);
    // Do NOT unregister from the analyzer/decoder/exporter registries here:
    // at static teardown those singletons may already be destroyed, and
    // touching them is UB (segfault). Plugins are process-lifetime; the OS
    // reclaims the loaded module handles on exit, and the registries tear
    // themselves down independently.
    m_plugins.clear();
}

std::string PluginManager::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

std::vector<std::string> PluginManager::scanDirectory(const std::string &dirPath)
{
    return mviewer::core::scanPluginCandidates(dirPath);
}

namespace
{
using AbiFn = const PluginABI *(*)();
using VersionFn = int (*)();
using AnalyzerCreateFn = Analyzer *(*)();
using AnalyzerDestroyFn = void (*)(Analyzer *);
using DecoderCreateFn = IDecoder *(*)();
using DecoderDestroyFn = void (*)(IDecoder *);
using ExporterCreateFn = IExporter *(*)();
using ExporterDestroyFn = void (*)(IExporter *);
using ImporterCreateFn = IImporter *(*)();
using ImporterDestroyFn = void (*)(IImporter *);
using CompareCreateFn = mviewer::core::ICompareAlgorithm *(*)();
using CompareDestroyFn = void (*)(mviewer::core::ICompareAlgorithm *);
using NameFn = const char *(*)();

struct PluginSymbols
{
    AbiFn abi = nullptr;
    VersionFn version = nullptr;
    AnalyzerCreateFn createAnalyzer = nullptr;
    AnalyzerDestroyFn destroyAnalyzer = nullptr;
    DecoderCreateFn createDecoder = nullptr;
    DecoderDestroyFn destroyDecoder = nullptr;
    ExporterCreateFn createExporter = nullptr;
    ExporterDestroyFn destroyExporter = nullptr;
    ImporterCreateFn createImporter = nullptr;
    ImporterDestroyFn destroyImporter = nullptr;
    CompareCreateFn createCompare = nullptr;
    CompareDestroyFn destroyCompare = nullptr;
    NameFn name = nullptr;
};

struct OpenPlugin
{
    PluginHandle handle = nullptr;
    PluginSymbols symbols;
};

template <typename Fn> Fn lookupSymbol(PluginHandle handle, const char *name)
{
#ifdef _WIN32
    return reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<Fn>(dlsym(handle, name));
#endif
}

void closePluginHandle(PluginHandle handle)
{
    if (!handle)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void logRejectedPlugin(const std::string &path, const std::string &reason)
{
    qWarning().nospace().noquote()
        << "[PluginManager] Rejected: " << QString::fromUtf8(path.c_str()) << ": "
        << QString::fromUtf8(reason.c_str());
}

// One extra loader reference. The host's original dlopen/LoadLibrary is kept
// for the process lifetime (ADR-017) and is never closed here. Dropping this
// pin releases only the extra reference, so a live Analyzer deleter can keep
// the module mapped even if the host handle were closed.
struct PluginModulePin
{
    PluginHandle handle = nullptr;
    PluginModulePin() = default;
    ~PluginModulePin()
    {
        closePluginHandle(handle);
    }
    PluginModulePin(const PluginModulePin &) = delete;
    PluginModulePin &operator=(const PluginModulePin &) = delete;
};

PluginHandle openPluginHandle(const std::string &path, std::string &error)
{
    return reinterpret_cast<PluginHandle>(mviewer::core::loadPluginLibrary(path, error));
}

std::shared_ptr<PluginModulePin> pinPluginModule(const std::string &path)
{
    auto pin = std::make_shared<PluginModulePin>();
    std::string ignored;
    pin->handle = openPluginHandle(path, ignored);
    if (!pin->handle)
        return nullptr;
    return pin;
}

AnalyzerDeleter makeAnalyzerDeleter(const std::shared_ptr<PluginModulePin> &module,
                                    AnalyzerDestroyFn destroy)
{
    return [module, destroy](Analyzer *value)
    {
        // `module` owns an extra loader reference so destroyAnalyzer still
        // runs inside the plugin while this instance exists.
        if (!module || module->handle == nullptr || destroy == nullptr)
            return;
        if (!value)
            return;
        std::string ignored;
        mviewer::core::invokePluginVoid("destroyAnalyzer", ignored, [&] { destroy(value); });
    };
}

bool rejectAbiMismatch(const std::string &path, OpenPlugin &plugin, const std::string &reason,
                       std::string &error)
{
    error = reason;
    logRejectedPlugin(path, reason);
    closePluginHandle(plugin.handle);
    plugin.handle = nullptr;
    return false;
}

bool openPlugin(const std::string &path, OpenPlugin &plugin, std::string &error)
{
    plugin.handle = openPluginHandle(path, error);
    if (!plugin.handle)
        return false;

    auto &s = plugin.symbols;
    // Version gate first. No other plugin symbol is resolved or called until
    // the ABI (or the legacy version export) is accepted.
    s.abi = lookupSymbol<AbiFn>(plugin.handle, "mviewer_plugin_abi");
    s.version = lookupSymbol<VersionFn>(plugin.handle, "mviewer_plugin_api_version");

    if (s.abi)
    {
        bool abiOk = false;
        const PluginABI *abi = mviewer::core::invokePluginCallback("mviewer_plugin_abi", error,
                                                                   abiOk, [&] { return s.abi(); });
        if (!abiOk)
            return rejectAbiMismatch(path, plugin, error, error);
        if (!abi)
            return rejectAbiMismatch(path, plugin, "plugin ABI descriptor is null", error);
        if (!pluginABICompatible(hostPluginABI(), *abi))
        {
            const std::string reason =
                "plugin ABI incompatible: host abi=" + std::to_string(hostPluginABI().abiVersion) +
                " api=" + std::to_string(hostPluginABI().apiVersion) +
                ", plugin abi=" + std::to_string(abi->abiVersion) +
                " api=" + std::to_string(abi->apiVersion);
            return rejectAbiMismatch(path, plugin, reason, error);
        }
        const std::string warning = pluginABIWarnings(hostPluginABI(), *abi);
        if (!warning.empty())
            std::cout << "[PluginManager] Warning: " << warning << '\n';
    }
    else if (!s.version)
    {
        return rejectAbiMismatch(path, plugin, mviewer::core::kPluginMissingAbiError, error);
    }
    else
    {
        bool versionOk = false;
        const int version = mviewer::core::invokePluginCallback(
            "mviewer_plugin_api_version", error, versionOk, [&] { return s.version(); });
        if (!versionOk)
            return rejectAbiMismatch(path, plugin, error, error);
        if (version != MVIEWER_PLUGIN_API_VERSION)
        {
            return rejectAbiMismatch(path, plugin, "plugin API version mismatch (legacy export)",
                                     error);
        }
    }

    s.createAnalyzer = lookupSymbol<AnalyzerCreateFn>(plugin.handle, "createAnalyzer");
    s.destroyAnalyzer = lookupSymbol<AnalyzerDestroyFn>(plugin.handle, "destroyAnalyzer");
    s.createDecoder = lookupSymbol<DecoderCreateFn>(plugin.handle, "createDecoder");
    s.destroyDecoder = lookupSymbol<DecoderDestroyFn>(plugin.handle, "destroyDecoder");
    s.createExporter = lookupSymbol<ExporterCreateFn>(plugin.handle, "createExporter");
    s.destroyExporter = lookupSymbol<ExporterDestroyFn>(plugin.handle, "destroyExporter");
    s.createImporter = lookupSymbol<ImporterCreateFn>(plugin.handle, "createImporter");
    s.destroyImporter = lookupSymbol<ImporterDestroyFn>(plugin.handle, "destroyImporter");
    s.createCompare = lookupSymbol<CompareCreateFn>(plugin.handle, "createCompareAlgorithm");
    s.destroyCompare = lookupSymbol<CompareDestroyFn>(plugin.handle, "destroyCompareAlgorithm");
    s.name = lookupSymbol<NameFn>(plugin.handle, "pluginName");
    return true;
}

void recordPlugin(const std::string &path, const std::string &displayName, PluginHandle handle,
                  const std::string &kind, const std::string &id,
                  std::string PluginManager::PluginEntry::*slot,
                  std::unordered_map<std::string, PluginManager::PluginEntry> &plugins)
{
    PluginManager::PluginEntry entry;
    entry.path = path;
    entry.name = displayName;
    entry.*slot = id;
    entry.handle = handle;
    entry.loaded = true;
    plugins[path] = entry;
    std::cout << "[PluginManager] Loaded: " << displayName << " (" << kind << ": " << id
              << ") from " << path << '\n';
}

bool registerAnalyzerPlugin(const std::string &path, const std::string &displayName,
                            OpenPlugin &plugin,
                            std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                            std::string &error)
{
    const auto create = plugin.symbols.createAnalyzer;
    const auto destroy = plugin.symbols.destroyAnalyzer;
    if (!create || !destroy)
    {
        error = "destroyAnalyzer export not found for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }

    const std::shared_ptr<PluginModulePin> module = pinPluginModule(path);
    if (!module)
    {
        error = "failed to pin plugin module for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }

    // Probe is destroyed through the plugin deleter, same as later instances.
    bool createOk = false;
    Analyzer *created = mviewer::core::invokePluginCallback("createAnalyzer", error, createOk,
                                                            [&] { return create(); });
    if (!createOk)
    {
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    std::unique_ptr<Analyzer, AnalyzerDeleter> probe(created, makeAnalyzerDeleter(module, destroy));
    if (!probe)
    {
        error = "createAnalyzer returned null for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    bool nameOk = false;
    const std::string id = mviewer::core::invokePluginCallback("Analyzer::name", error, nameOk,
                                                               [&] { return probe->name(); });
    if (!nameOk)
    {
        probe.reset();
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    probe.reset();

    AnalyzerRegistry::instance().registerAnalyzer(
        id,
        [module, create, destroy]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
        {
            // Same C++ catch as the probe. Not a sandbox: the plugin is in-process.
            std::string ignored;
            bool ok = false;
            Analyzer *analyzer = mviewer::core::invokePluginCallback("createAnalyzer", ignored, ok,
                                                                     [&] { return create(); });
            if (!ok || !analyzer)
                return nullptr;
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(analyzer,
                                                              makeAnalyzerDeleter(module, destroy));
        });
    recordPlugin(path, displayName, plugin.handle, "analyzer", id,
                 &PluginManager::PluginEntry::analyzerId, plugins);
    return true;
}

void unregisterPlugin(const PluginManager::PluginEntry &entry)
{
    if (!entry.analyzerId.empty())
        AnalyzerRegistry::instance().unregister(entry.analyzerId);
    if (!entry.decoderId.empty())
        DecoderRegistry::instance().unregister(entry.decoderId);
    if (!entry.exporterId.empty())
        ExporterRegistry::instance().unregister(entry.exporterId);
    if (!entry.importerId.empty())
        ImporterRegistry::instance().unregister(entry.importerId);
    // Compare-algorithm plugins are probed for their id but not inserted into
    // a registry, so there is no factory to drop for compareAlgorithmId.
}

template <typename Interface, typename CreateFn, typename DestroyFn, typename RegisterFn>
bool registerSharedPlugin(const std::string &path, const std::string &displayName,
                          OpenPlugin &plugin, const char *kind, CreateFn create, DestroyFn destroy,
                          RegisterFn registerFn, std::string PluginManager::PluginEntry::*slot,
                          std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                          std::string &error)
{
    if (!create || !destroy)
    {
        error = std::string("destroy") + kind + " export not found for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    bool createOk = false;
    Interface *object =
        mviewer::core::invokePluginCallback(kind, error, createOk, [&] { return create(); });
    if (!createOk)
    {
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    if (!object)
    {
        error = std::string("create") + kind + " returned null for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    bool nameOk = false;
    const std::string id =
        mviewer::core::invokePluginCallback("name", error, nameOk, [&] { return object->name(); });
    if (!nameOk)
    {
        std::string ignored;
        mviewer::core::invokePluginVoid("destroy", ignored, [&] { destroy(object); });
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    std::shared_ptr<Interface> owned(object,
                                     [destroy](Interface *value)
                                     {
                                         if (!value)
                                             return;
                                         std::string ignored;
                                         mviewer::core::invokePluginVoid("destroy", ignored,
                                                                         [&] { destroy(value); });
                                     });
    registerFn(owned);
    recordPlugin(path, displayName, plugin.handle, kind, id, slot, plugins);
    return true;
}

bool registerComparePlugin(const std::string &path, const std::string &displayName,
                           OpenPlugin &plugin,
                           std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                           std::string &error)
{
    if (!plugin.symbols.createCompare || !plugin.symbols.destroyCompare)
    {
        error = "destroyCompareAlgorithm export not found for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    bool createOk = false;
    auto *algorithm = mviewer::core::invokePluginCallback(
        "createCompareAlgorithm", error, createOk, [&] { return plugin.symbols.createCompare(); });
    if (!createOk)
    {
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    if (!algorithm)
    {
        error = "createCompareAlgorithm returned null for " + path;
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    bool nameOk = false;
    const std::string id = mviewer::core::invokePluginCallback(
        "ICompareAlgorithm::name", error, nameOk, [&] { return algorithm->name(); });
    if (!nameOk)
    {
        std::string ignored;
        mviewer::core::invokePluginVoid("destroyCompareAlgorithm", ignored,
                                        [&] { plugin.symbols.destroyCompare(algorithm); });
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    if (!mviewer::core::invokePluginVoid("destroyCompareAlgorithm", error,
                                         [&] { plugin.symbols.destroyCompare(algorithm); }))
    {
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
    recordPlugin(path, displayName, plugin.handle, "compareAlgorithm", id,
                 &PluginManager::PluginEntry::compareAlgorithmId, plugins);
    return true;
}

bool finishPluginLoad(const std::string &path, OpenPlugin &plugin,
                      std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                      std::string &error)
{
    std::string displayName = mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(path).stem());
    if (plugin.symbols.name)
    {
        bool nameOk = false;
        const char *raw = mviewer::core::invokePluginCallback("pluginName", error, nameOk, [&]
                                                              { return plugin.symbols.name(); });
        if (!nameOk)
        {
            closePluginHandle(plugin.handle);
            plugin.handle = nullptr;
            return false;
        }
        if (!raw)
        {
            error = "pluginName returned null for " + path;
            closePluginHandle(plugin.handle);
            plugin.handle = nullptr;
            return false;
        }
        displayName = raw;
    }
    if (plugin.symbols.createAnalyzer)
        return registerAnalyzerPlugin(path, displayName, plugin, plugins, error);
    if (plugin.symbols.createDecoder)
        return registerSharedPlugin<IDecoder>(
            path, displayName, plugin, "Decoder", plugin.symbols.createDecoder,
            plugin.symbols.destroyDecoder, [](std::shared_ptr<IDecoder> value)
            { DecoderRegistry::instance().registerDecoder(std::move(value)); },
            &PluginManager::PluginEntry::decoderId, plugins, error);
    if (plugin.symbols.createExporter)
        return registerSharedPlugin<IExporter>(
            path, displayName, plugin, "Exporter", plugin.symbols.createExporter,
            plugin.symbols.destroyExporter, [](std::shared_ptr<IExporter> value)
            { ExporterRegistry::instance().registerExporter(std::move(value)); },
            &PluginManager::PluginEntry::exporterId, plugins, error);
    if (plugin.symbols.createImporter)
        return registerSharedPlugin<IImporter>(
            path, displayName, plugin, "Importer", plugin.symbols.createImporter,
            plugin.symbols.destroyImporter, [](std::shared_ptr<IImporter> value)
            { ImporterRegistry::instance().registerImporter(std::move(value)); },
            &PluginManager::PluginEntry::importerId, plugins, error);
    if (plugin.symbols.createCompare)
        return registerComparePlugin(path, displayName, plugin, plugins, error);
    error = "plugin exposes no supported create* export "
            "(analyzer/decoder/exporter/importer/compareAlgorithm)";
    closePluginHandle(plugin.handle);
    plugin.handle = nullptr;
    return false;
}
} // namespace

bool PluginManager::load(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const mviewer::core::PluginFileAssessment file = mviewer::core::assessPluginFile(path, {});
    if (!file.regularFile)
    {
        m_lastError = file.error.empty() ? "plugin path is not a regular file" : file.error;
        return false;
    }
    if (m_plugins.count(path))
    {
        m_lastError = "already loaded: " + path;
        return false;
    }
    OpenPlugin plugin;
    if (!openPlugin(path, plugin, m_lastError))
        return false;
    return finishPluginLoad(path, plugin, m_plugins, m_lastError);
}

int PluginManager::loadDirectory(const std::string &)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Scanning must not map unknown modules. Startup uses loadTrusted for
    // hashes already stored. A new file is mapped only by commitExplicitTrust.
    m_lastError = "refusing to map untrusted modules from a directory";
    return 0;
}

int PluginManager::loadTrusted(const std::string &dirPath,
                               const std::vector<std::string> &trustedSha256Hex)
{
    std::unordered_set<std::string> trusted;
    for (const std::string &hex : trustedSha256Hex)
    {
        const std::string normalized = mviewer::core::normalizePluginSha256(hex);
        if (mviewer::core::isPluginSha256Hex(normalized))
            trusted.insert(normalized);
    }

    int count = 0;
    for (const std::string &path : scanDirectory(dirPath))
    {
        std::string hashError;
        const std::string hash = mviewer::core::pluginFileSha256(path, hashError);
        if (!mviewer::core::isPluginSha256Hex(hash) || trusted.count(hash) == 0)
            continue;
        if (load(path))
            ++count;
    }
    return count;
}

std::vector<ExplicitPluginTrustResult>
PluginManager::commitExplicitTrust(const std::vector<ExplicitPluginTrust> &confirmed, bool accepted)
{
    std::vector<ExplicitPluginTrustResult> results;
    results.reserve(confirmed.size());
    for (const ExplicitPluginTrust &item : confirmed)
    {
        ExplicitPluginTrustResult result;
        result.path = item.path;
        // Cancel, discovery, and any caller that has not received a Yes.
        if (!accepted)
        {
            results.push_back(result);
            continue;
        }
        const std::string confirmedHash = mviewer::core::normalizePluginSha256(item.sha256Hex);
        std::string hashError;
        const std::string current = mviewer::core::pluginFileSha256(item.path, hashError);
        // Already mapped: do not open the path again and do not bind a new hash
        // to bytes that may have replaced the file.
        if (!mviewer::core::isPluginSha256Hex(confirmedHash) || current != confirmedHash ||
            isLoaded(item.path))
        {
            results.push_back(result);
            continue;
        }
        if (!load(item.path))
        {
            results.push_back(result);
            continue;
        }
        result.loaded = true;
        const std::string after = mviewer::core::pluginFileSha256(item.path, hashError);
        // Persist only the hash the caller confirmed. A mismatch means the
        // bytes changed around the map. That race is not closed here.
        if (after == confirmedHash)
        {
            result.persist = true;
            result.sha256Hex = after;
        }
        results.push_back(result);
    }
    return results;
}

bool PluginManager::unload(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_plugins.find(path);
    if (it == m_plugins.end())
        return false;

    // Drop registry entries so no factory or instance still points into the
    // plugin after it is released. Importers are included; unloadAll uses the
    // same helper.
    unregisterPlugin(it->second);

    // NOTE: we intentionally do NOT FreeLibrary/dlclose here. Unloading a
    // Qt-linking plugin DLL at runtime (or during process teardown) is unsafe
    // on Windows — the OS DLL detach / CRT static ordering crashes the process.
    // Plugins are process-lifetime; the handle is reclaimed by the OS at exit.
    m_plugins.erase(it);

    std::cout << "[PluginManager] Released: " << path << '\n';
    return true;
}

void PluginManager::unloadAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &item : m_plugins)
        unregisterPlugin(item.second);
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
