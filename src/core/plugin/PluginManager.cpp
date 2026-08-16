#include "core/plugin/PluginManager.h"
#include "core/compare/ICompareAlgorithm.h"
#include "core/export/ExporterRegistry.h"
#include "core/export/IExporter.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/import/IImporter.h"
#include "core/import/ImporterRegistry.h"
#include "core/plugin/PluginABI.h"

#include <filesystem>
#include <iostream>
#include <string>

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

template <typename Fn>
Fn lookupSymbol(PluginHandle handle, const char *name)
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

bool openPlugin(const std::string &path, OpenPlugin &plugin, std::string &error)
{
#ifdef _WIN32
    plugin.handle = reinterpret_cast<PluginHandle>(LoadLibraryA(path.c_str()));
    if (!plugin.handle)
    {
        error = "LoadLibrary failed: " + std::to_string(GetLastError());
        return false;
    }
#else
    plugin.handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!plugin.handle)
    {
        error = "dlopen failed: " + std::string(dlerror());
        return false;
    }
#endif
    auto &s = plugin.symbols;
    s.abi = lookupSymbol<AbiFn>(plugin.handle, "mviewer_plugin_abi");
    s.version = lookupSymbol<VersionFn>(plugin.handle, "mviewer_plugin_api_version");
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

    if (s.abi)
    {
        const PluginABI *abi = s.abi();
        if (!pluginABICompatible(hostPluginABI(), *abi))
        {
            error = "plugin ABI incompatible: host abi=" +
                    std::to_string(hostPluginABI().abiVersion) +
                    " api=" + std::to_string(hostPluginABI().apiVersion) +
                    ", plugin abi=" + std::to_string(abi->abiVersion) +
                    " api=" + std::to_string(abi->apiVersion);
            closePluginHandle(plugin.handle);
            plugin.handle = nullptr;
            return false;
        }
        const std::string warning = pluginABIWarnings(hostPluginABI(), *abi);
        if (!warning.empty())
            std::cout << "[PluginManager] Warning: " << warning << std::endl;
    }
#ifdef _WIN32
    else if (s.version && s.version() != MVIEWER_PLUGIN_API_VERSION)
    {
        error = "plugin API version mismatch (legacy export)";
        closePluginHandle(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
#endif
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
              << ") from " << path << std::endl;
}

bool registerAnalyzerPlugin(const std::string &path, const std::string &displayName,
                            OpenPlugin &plugin,
                            std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                            std::string &error)
{
    Analyzer *probe = plugin.symbols.createAnalyzer();
    if (!probe)
    {
        error = "createAnalyzer returned null for " + path;
        closePluginHandle(plugin.handle);
        return false;
    }
    const std::string id = probe->name();
    delete probe;
    const auto create = plugin.symbols.createAnalyzer;
    const auto destroy = plugin.symbols.destroyAnalyzer;
    AnalyzerRegistry::instance().registerAnalyzer(
        id,
        [create, destroy]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
        {
            Analyzer *analyzer = create();
            if (!analyzer)
                return nullptr;
            if (destroy)
                return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                    analyzer,
                    [destroy](Analyzer *value)
                    {
                        if (value)
                            destroy(value);
                    });
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                analyzer, [](Analyzer *value) { delete value; });
        });
    recordPlugin(path, displayName, plugin.handle, "analyzer", id,
                 &PluginManager::PluginEntry::analyzerId, plugins);
    return true;
}

template <typename Interface, typename CreateFn, typename DestroyFn, typename RegisterFn>
bool registerSharedPlugin(const std::string &path, const std::string &displayName,
                          OpenPlugin &plugin, const char *kind,
                          CreateFn create, DestroyFn destroy, RegisterFn registerFn,
                          std::string PluginManager::PluginEntry::*slot,
                          std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                          std::string &error)
{
    Interface *object = create();
    if (!object)
    {
        error = std::string("create") + kind + " returned null for " + path;
        closePluginHandle(plugin.handle);
        return false;
    }
    const std::string id = object->name();
    std::shared_ptr<Interface> owned =
        destroy
            ? std::shared_ptr<Interface>(object, [destroy](Interface *value)
                                         {
                                             if (value)
                                                 destroy(value);
                                         })
            : std::shared_ptr<Interface>(object, [](Interface *value) { delete value; });
    registerFn(owned);
    recordPlugin(path, displayName, plugin.handle, kind, id, slot, plugins);
    return true;
}

bool registerComparePlugin(const std::string &path, const std::string &displayName,
                           OpenPlugin &plugin,
                           std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                           std::string &error)
{
    auto *algorithm = plugin.symbols.createCompare();
    if (!algorithm)
    {
        error = "createCompareAlgorithm returned null for " + path;
        closePluginHandle(plugin.handle);
        return false;
    }
    const std::string id = algorithm->name();
    if (plugin.symbols.destroyCompare)
        plugin.symbols.destroyCompare(algorithm);
    else
        delete algorithm;
    recordPlugin(path, displayName, plugin.handle, "compareAlgorithm", id,
                 &PluginManager::PluginEntry::compareAlgorithmId, plugins);
    return true;
}

bool finishPluginLoad(const std::string &path, OpenPlugin &plugin,
                      std::unordered_map<std::string, PluginManager::PluginEntry> &plugins,
                      std::string &error)
{
    const std::string displayName =
        plugin.symbols.name ? plugin.symbols.name() : std::filesystem::path(path).stem().string();
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
    return false;
}
} // namespace

bool PluginManager::load(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
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

    // Drop registry entries so no dangling factory (pointing into the plugin
    // module) remains in any registry.
    if (!it->second.analyzerId.empty())
        AnalyzerRegistry::instance().unregister(it->second.analyzerId);
    if (!it->second.decoderId.empty())
        DecoderRegistry::instance().unregister(it->second.decoderId);
    if (!it->second.exporterId.empty())
        ExporterRegistry::instance().unregister(it->second.exporterId);
    if (!it->second.importerId.empty())
        ImporterRegistry::instance().unregister(it->second.importerId);

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
    {
        if (!entry.analyzerId.empty())
            AnalyzerRegistry::instance().unregister(entry.analyzerId);
        if (!entry.decoderId.empty())
            DecoderRegistry::instance().unregister(entry.decoderId);
        if (!entry.exporterId.empty())
            ExporterRegistry::instance().unregister(entry.exporterId);
    }
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
