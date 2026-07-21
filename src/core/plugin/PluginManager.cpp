#include "core/plugin/PluginManager.h"
#include "core/plugin/PluginABI.h"
#include "core/image/decoder/IDecoder.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/export/IExporter.h"
#include "core/export/ExporterRegistry.h"

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

    auto abiFn =
        reinterpret_cast<const PluginABI *(*)()>(GetProcAddress(handle, "mviewer_plugin_abi"));
    auto versionFn = reinterpret_cast<int (*)()>(GetProcAddress(handle, "mviewer_plugin_api_version"));
    auto createAnalyzerFn =
        reinterpret_cast<Analyzer *(*)()>(GetProcAddress(handle, "createAnalyzer"));
    auto destroyAnalyzerFn =
        reinterpret_cast<void (*)(Analyzer *)>(GetProcAddress(handle, "destroyAnalyzer"));
    auto createDecoderFn =
        reinterpret_cast<IDecoder *(*)()>(GetProcAddress(handle, "createDecoder"));
    auto destroyDecoderFn =
        reinterpret_cast<void (*)(IDecoder *)>(GetProcAddress(handle, "destroyDecoder"));
    auto createExporterFn =
        reinterpret_cast<IExporter *(*)()>(GetProcAddress(handle, "createExporter"));
    auto destroyExporterFn =
        reinterpret_cast<void (*)(IExporter *)>(GetProcAddress(handle, "destroyExporter"));
    auto nameFn = reinterpret_cast<const char *(*)()>(GetProcAddress(handle, "pluginName"));

    // M14.2: ABI triple gate (frozen for v1.x). Reject before probing an instance.
    if (abiFn)
    {
        const PluginABI *pabi = abiFn();
        if (!pluginABICompatible(hostPluginABI(), *pabi))
        {
            m_lastError = "plugin ABI incompatible: host abi=" +
                          std::to_string(hostPluginABI().abiVersion) +
                          " api=" + std::to_string(hostPluginABI().apiVersion) +
                          ", plugin abi=" + std::to_string(pabi->abiVersion) +
                          " api=" + std::to_string(pabi->apiVersion);
            FreeLibrary(handle);
            return false;
        }
        std::string warn = pluginABIWarnings(hostPluginABI(), *pabi);
        if (!warn.empty())
            std::cout << "[PluginManager] Warning: " << warn << std::endl;
    }
    else if (versionFn && versionFn() != MVIEWER_PLUGIN_API_VERSION)
    {
        m_lastError = "plugin API version mismatch (legacy export)";
        return false;
    }
#else
    void *handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle)
    {
        m_lastError = "dlopen failed: " + std::string(dlerror());
        return false;
    }

    auto abiFn = reinterpret_cast<const PluginABI *(*)()>(dlsym(handle, "mviewer_plugin_abi"));
    auto versionFn = reinterpret_cast<int (*)()>(dlsym(handle, "mviewer_plugin_api_version"));
    auto createAnalyzerFn = reinterpret_cast<Analyzer *(*)()>(dlsym(handle, "createAnalyzer"));
    auto destroyAnalyzerFn = reinterpret_cast<void (*)(Analyzer *)>(dlsym(handle, "destroyAnalyzer"));
    auto createDecoderFn = reinterpret_cast<IDecoder *(*)()>(dlsym(handle, "createDecoder"));
    auto destroyDecoderFn = reinterpret_cast<void (*)(IDecoder *)>(dlsym(handle, "destroyDecoder"));
    auto createExporterFn = reinterpret_cast<IExporter *(*)()>(dlsym(handle, "createExporter"));
    auto destroyExporterFn = reinterpret_cast<void (*)(IExporter *)>(dlsym(handle, "destroyExporter"));
    auto nameFn = reinterpret_cast<const char *(*)()>(dlsym(handle, "pluginName"));

    // M14.2: ABI triple gate (mirrors the Windows branch).
    if (abiFn)
    {
        const PluginABI *pabi = abiFn();
        if (!pluginABICompatible(hostPluginABI(), *pabi))
        {
            m_lastError = "plugin ABI incompatible: host abi=" +
                          std::to_string(hostPluginABI().abiVersion) +
                          " api=" + std::to_string(hostPluginABI().apiVersion) +
                          ", plugin abi=" + std::to_string(pabi->abiVersion) +
                          " api=" + std::to_string(pabi->apiVersion);
            return false;
        }
        std::string warn = pluginABIWarnings(hostPluginABI(), *pabi);
        if (!warn.empty())
            std::cout << "[PluginManager] Warning: " << warn << std::endl;
    }
#endif

    const std::string displayName =
        nameFn ? nameFn() : std::filesystem::path(path).stem().string();

    // --- Analyzer plugin (existing contract) ---
    if (createAnalyzerFn)
    {
        Analyzer *analyzer = createAnalyzerFn();
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

        AnalyzerRegistry::instance().registerAnalyzer(
            analyzerId,
            [createAnalyzerFn, destroyAnalyzerFn]() -> std::unique_ptr<Analyzer, AnalyzerDeleter>
            {
                Analyzer *a = createAnalyzerFn();
                if (!a)
                    return nullptr;
                if (destroyAnalyzerFn)
                    return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                        a, [destroyAnalyzerFn](Analyzer *p) { if (p) destroyAnalyzerFn(p); });
                return std::unique_ptr<Analyzer, AnalyzerDeleter>(a, [](Analyzer *p) { delete p; });
            });

        PluginEntry entry;
        entry.path = path;
        entry.name = displayName;
        entry.analyzerId = analyzerId;
        entry.handle = handle;
        entry.loaded = true;
        m_plugins[path] = entry;
        std::cout << "[PluginManager] Loaded: " << displayName << " (analyzer: " << analyzerId
                  << ") from " << path << std::endl;
        return true;
    }

    // --- Decoder plugin (M14.3 unified loader) ---
    if (createDecoderFn)
    {
        IDecoder *dec = createDecoderFn();
        if (!dec)
        {
            m_lastError = "createDecoder returned null for " + path;
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle));
#else
            dlclose(handle);
#endif
            return false;
        }
        const std::string decoderId = dec->name();
        auto decPtr = destroyDecoderFn
                          ? std::shared_ptr<IDecoder>(
                                dec, [destroyDecoderFn](IDecoder *p) { if (p) destroyDecoderFn(p); })
                          : std::shared_ptr<IDecoder>(dec, [](IDecoder *p) { delete p; });
        dec = nullptr;
        DecoderRegistry::instance().registerDecoder(decPtr);

        PluginEntry entry;
        entry.path = path;
        entry.name = displayName;
        entry.decoderId = decoderId;
        entry.handle = handle;
        entry.loaded = true;
        m_plugins[path] = entry;
        std::cout << "[PluginManager] Loaded: " << displayName << " (decoder: " << decoderId
                  << ") from " << path << std::endl;
        return true;
    }

    // --- Exporter plugin (M14.3 unified loader) ---
    if (createExporterFn)
    {
        IExporter *exp = createExporterFn();
        if (!exp)
        {
            m_lastError = "createExporter returned null for " + path;
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle));
#else
            dlclose(handle);
#endif
            return false;
        }
        const std::string exporterId = exp->name();
        auto expPtr = destroyExporterFn
                          ? std::shared_ptr<IExporter>(
                                exp, [destroyExporterFn](IExporter *p) { if (p) destroyExporterFn(p); })
                          : std::shared_ptr<IExporter>(exp, [](IExporter *p) { delete p; });
        exp = nullptr;
        ExporterRegistry::instance().registerExporter(expPtr);

        PluginEntry entry;
        entry.path = path;
        entry.name = displayName;
        entry.exporterId = exporterId;
        entry.handle = handle;
        entry.loaded = true;
        m_plugins[path] = entry;
        std::cout << "[PluginManager] Loaded: " << displayName << " (exporter: " << exporterId
                  << ") from " << path << std::endl;
        return true;
    }

    m_lastError = "plugin exposes no supported create* export (analyzer/decoder/exporter)";
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
    return false;
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
