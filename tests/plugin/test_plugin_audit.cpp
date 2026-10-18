// Regression coverage for plugin probe lifetime, destroyAnalyzer, importer
// unregistration, and the cross-platform ABI gate (audit #36-#39).
#include "core/analyzer/Analyzer.h"
#include "core/filesystem/Utf8Path.h"
#include "core/import/ImporterRegistry.h"
#include "core/plugin/PluginLoader.h"
#include "core/plugin/PluginManager.h"

#include <QCoreApplication>
#include <QString>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <dlfcn.h>
#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0
#endif
#endif

static int g_fail = 0;
static std::string g_qtLog;

static void qtLogHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_qtLog += message.toStdString();
    g_qtLog.push_back('\n');
    std::cerr << message.toStdString() << '\n';
}

static void check(bool cond, const std::string &msg)
{
    if (!cond)
    {
        std::cerr << "FAIL: " << msg << '\n';
        ++g_fail;
    }
    else
    {
        std::cout << "PASS: " << msg << '\n';
    }
}

namespace
{

class EnvGuard
{
  public:
    EnvGuard(const char *key, const std::string &value) : m_key(key)
    {
#ifdef _WIN32
        SetEnvironmentVariableA(m_key.c_str(), value.c_str());
#else
        setenv(m_key.c_str(), value.c_str(), 1);
#endif
    }
    ~EnvGuard()
    {
#ifdef _WIN32
        SetEnvironmentVariableA(m_key.c_str(), nullptr);
#else
        unsetenv(m_key.c_str());
#endif
    }
    EnvGuard(const EnvGuard &) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;

  private:
    std::string m_key;
};

std::filesystem::path exeDir(const char *argv0)
{
    const std::filesystem::path self(argv0 ? argv0 : "");
    if (self.has_parent_path())
        return self.parent_path();
    return std::filesystem::current_path();
}

std::string utf8Absolute(const std::filesystem::path &path)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return mviewer::core::pathToUtf8(ec ? path : absolute);
}

std::string pluginFile(const std::filesystem::path &dir, const char *name)
{
#if defined(_WIN32)
    const char *ext = ".dll";
#elif defined(__APPLE__)
    const char *ext = ".dylib";
#else
    const char *ext = ".so";
#endif
    return utf8Absolute(dir / (std::string(name) + ext));
}

struct Counts
{
    int creates = -1;
    int destroys = -1;
    int live = -1;
    bool ok = false;
};

int loadSymbol(const std::string &path, const char *symbol, bool &ok)
{
    ok = false;
#ifdef _WIN32
    const std::wstring wide = mviewer::core::pathFromUtf8(path).wstring();
    HMODULE mod = GetModuleHandleW(wide.c_str());
    if (!mod)
        return -1;
    auto *fn = reinterpret_cast<int (*)()>(GetProcAddress(mod, symbol));
    if (!fn)
        return -1;
    ok = true;
    return fn();
#else
    void *mod = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (!mod)
        mod = dlopen(path.c_str(), RTLD_LAZY);
    if (!mod)
        return -1;
    auto *fn = reinterpret_cast<int (*)()>(dlsym(mod, symbol));
    if (!fn)
    {
        dlclose(mod);
        return -1;
    }
    const int value = fn();
    dlclose(mod);
    ok = true;
    return value;
#endif
}

Counts readCounts(const std::string &path)
{
    Counts counts;
    bool createsOk = false;
    bool destroysOk = false;
    bool liveOk = false;
    counts.creates = loadSymbol(path, "auditAnalyzerCreateCalls", createsOk);
    counts.destroys = loadSymbol(path, "auditAnalyzerDestroyCalls", destroysOk);
    counts.live = loadSymbol(path, "auditAnalyzerLive", liveOk);
    counts.ok = createsOk && destroysOk && liveOk;
    return counts;
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        return {};
    std::string data;
    std::string line;
    while (std::getline(in, line))
    {
        data += line;
        data.push_back('\n');
    }
    return data;
}

bool containsId(const std::vector<std::string> &ids, const std::string &id)
{
    for (const auto &candidate : ids)
    {
        if (candidate == id)
            return true;
    }
    return false;
}

} // namespace

static void testLoaderProbe(const std::string &path)
{
    auto loaded = PluginLoader::loadPlugin(path);
    check(loaded.loaded, loaded.loaded ? std::string("PluginLoader loads audit analyzer")
                                       : "PluginLoader loads audit analyzer: " + loaded.error);
    if (!loaded.loaded)
        return;

    const Counts afterLoad = readCounts(path);
    check(afterLoad.ok, "audit analyzer exports lifetime counters");
    check(afterLoad.creates == 1, "PluginLoader probe calls createAnalyzer once");
    check(afterLoad.destroys == 1, "PluginLoader probe is destroyed via destroyAnalyzer");
    check(afterLoad.live == 0, "PluginLoader probe is not left alive");

    auto instance = AnalyzerRegistry::instance().create("audit.probe");
    const Counts afterCreate = readCounts(path);
    check(instance != nullptr, "PluginLoader-registered analyzer can be created");
    check(afterCreate.creates == 2, "registry create calls the plugin create function");
    check(afterCreate.destroys == 1, "live instance is not destroyed early");
    check(afterCreate.live == 1, "one analyzer instance is alive");
    instance.reset();
    const Counts afterReset = readCounts(path);
    check(afterReset.destroys == 2, "registry instance is destroyed via destroyAnalyzer");
    check(afterReset.live == 0, "registry instance is gone after reset");
    AnalyzerRegistry::instance().unregister("audit.probe");
}

static void testManagerProbe(const std::string &path)
{
    auto &mgr = PluginManager::instance();
    const bool loaded = mgr.load(path);
    check(loaded, loaded ? std::string("PluginManager loads audit analyzer")
                         : "PluginManager loads audit analyzer: " + mgr.lastError());
    if (!loaded)
        return;

    const Counts afterLoad = readCounts(path);
    check(afterLoad.ok, "counters readable after PluginManager load");
    check(afterLoad.creates >= 1, "PluginManager probed the analyzer");
    check(afterLoad.destroys == afterLoad.creates,
          "every PluginManager create was released via destroyAnalyzer");
    check(afterLoad.live == 0, "PluginManager probe is not left alive");

    auto &registry = AnalyzerRegistry::instance();
    auto instance = registry.create("audit.probe");
    const Counts afterCreate = readCounts(path);
    check(instance != nullptr, "PluginManager-registered analyzer can be created");
    check(instance && instance->name() == "audit.probe", "created analyzer keeps its id");
    check(afterCreate.creates == afterLoad.creates + 1, "create bumps the plugin create count");
    check(afterCreate.destroys == afterLoad.destroys, "create does not destroy the new instance");
    check(afterCreate.live == 1, "instance stays alive after create");

    check(mgr.unload(path), "unload drops the audit analyzer registration");
    check(!containsId(registry.availableAnalyzers(), "audit.probe"),
          "analyzer id is gone from the registry after unload");
    check(registry.create("audit.probe") == nullptr, "create fails after unload");
    check(instance && instance->name() == "audit.probe",
          "live instance remains usable after unload");
    const Counts afterUnload = readCounts(path);
    check(afterUnload.live == 1, "unload does not destroy a live analyzer");
    check(afterUnload.destroys == afterCreate.destroys, "unload does not call destroyAnalyzer");

    instance.reset();
    const Counts afterReset = readCounts(path);
    check(afterReset.destroys == afterUnload.destroys + 1,
          "instance destroyed after unload still uses destroyAnalyzer");
    check(afterReset.live == 0, "no analyzer remains after the last instance is reset");
}

static void testRejectedPlugin(const std::string &path, const std::string &markerPath,
                               const char *errorNeedle, const char *label)
{
    std::error_code ec;
    std::filesystem::remove(mviewer::core::pathFromUtf8(markerPath), ec);
    g_qtLog.clear();
    EnvGuard guard("MVIEWER_AUDIT_SYMBOL_MARKER", markerPath);

    auto &mgr = PluginManager::instance();
    const bool loaded = mgr.load(path);
    check(!loaded, std::string(label) + " is rejected");
    check(mgr.lastError().find(errorNeedle) != std::string::npos,
          std::string(label) + " error mentions " + errorNeedle + ": " + mgr.lastError());
    check(g_qtLog.find("Rejected:") != std::string::npos,
          std::string(label) + " logs the rejection");
    check(g_qtLog.find(errorNeedle) != std::string::npos,
          std::string(label) + " log includes the reason");
    check(!mgr.isLoaded(path), std::string(label) + " is not recorded as loaded");

    const std::string marker = readText(mviewer::core::pathFromUtf8(markerPath));
    check(marker.empty(),
          std::string(label) + " did not call create/destroy/pluginName: " + marker);
}

static void testImporterUnload(const std::string &path)
{
    auto &mgr = PluginManager::instance();
    auto &importers = ImporterRegistry::instance();
    const bool loaded = mgr.load(path);
    check(loaded, loaded ? std::string("PluginManager loads example importer")
                         : "PluginManager loads example importer: " + mgr.lastError());
    if (!loaded)
        return;
    check(importers.get("folder-importer") != nullptr, "importer is registered after load");

    check(mgr.unload(path), "single unload removes the importer plugin");
    check(importers.get("folder-importer") == nullptr, "single unload unregisters the importer");

    const bool reloaded = mgr.load(path);
    check(reloaded, reloaded ? std::string("importer can be loaded again")
                             : "importer can be loaded again: " + mgr.lastError());
    check(importers.get("folder-importer") != nullptr, "importer is registered after reload");
    mgr.unloadAll();
    check(importers.get("folder-importer") == nullptr, "unloadAll unregisters the importer");
    check(mgr.count() == 0, "unloadAll clears the plugin table");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(qtLogHandler);
    std::cout << "[plugin audit tests]\n";

    const std::filesystem::path dir = exeDir(argc > 0 ? argv[0] : nullptr);
    const std::string audit = pluginFile(dir, "audit_analyzer");
    const std::string legacy = pluginFile(dir, "audit_legacy_mismatch");
    const std::string badAbi = pluginFile(dir, "example_analyzer_badabi");
    const std::string importer = pluginFile(dir, "example_importer");
    const std::string marker = utf8Absolute(dir / "audit_symbol_marker.txt");

    testLoaderProbe(audit);
    testManagerProbe(audit);
    testRejectedPlugin(badAbi, marker, "incompatible", "bad ABI plugin");
    check(!containsId(AnalyzerRegistry::instance().availableAnalyzers(), "example.bad_abi"),
          "bad ABI plugin is not registered");
    testRejectedPlugin(legacy, marker, "version mismatch", "legacy version mismatch");
    check(!containsId(AnalyzerRegistry::instance().availableAnalyzers(), "audit.legacy"),
          "legacy mismatch plugin is not registered");
    testImporterUnload(importer);

    if (g_fail)
    {
        std::cerr << g_fail << " FAILED\n";
        return 1;
    }
    std::cout << "plugin_audit_tests: all passed\n";
    return 0;
}
