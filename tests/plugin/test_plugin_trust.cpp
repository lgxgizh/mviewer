// P0-8 / P2-13: discovery does not map unknown modules, symlinks are not
// followed into dlopen, explicit load still works, and a C++ exception or
// null export is rejected without describing that catch as a sandbox.
#include "core/analyzer/Analyzer.h"
#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginManager.h"
#include "core/plugin/PluginPathPolicy.h"

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static int g_fail = 0;

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

std::filesystem::path exeDir(const char *argv0)
{
    const std::filesystem::path self(argv0 ? argv0 : "");
    if (self.has_parent_path())
        return self.parent_path();
    return std::filesystem::current_path();
}

std::string pluginExt()
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

std::string utf8(const std::filesystem::path &path)
{
    return mviewer::core::pathToUtf8(path);
}

bool containsId(const std::string &id)
{
    const auto ids = AnalyzerRegistry::instance().availableAnalyzers();
    for (const auto &candidate : ids)
    {
        if (candidate == id)
            return true;
    }
    return false;
}

bool moduleMapped(const std::string &path)
{
#ifdef _WIN32
    const std::wstring wide = mviewer::core::pathFromUtf8(path).wstring();
    return GetModuleHandleW(wide.c_str()) != nullptr;
#else
    void *handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (!handle)
        return false;
    dlclose(handle);
    return true;
#endif
}

void setEnv(const char *key, const std::string &value)
{
#ifdef _WIN32
    // UTF-16 process environment. audit_boundary reads it with
    // GetEnvironmentVariableW. The A API is not UTF-8, and it does not refresh
    // the CRT table that std::getenv consults inside the DLL.
    const std::wstring name = mviewer::core::pathFromUtf8(key).wstring();
    const std::wstring data = mviewer::core::pathFromUtf8(value).wstring();
    SetEnvironmentVariableW(name.c_str(), data.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path);
    std::string data;
    std::string line;
    while (std::getline(in, line))
    {
        data += line;
        data.push_back('\n');
    }
    return data;
}

bool markerHas(const std::string &marker, const char *needle)
{
    return marker.find(needle) != std::string::npos;
}

} // namespace

static void testHashDoesNotMap(PluginManager &mgr, const std::string &plain)
{
    std::string hashError;
    const std::string hash = mviewer::core::pluginFileSha256(plain, hashError);
    check(mviewer::core::isPluginSha256Hex(hash), "regular file has a SHA-256 before any load");
    const std::string wrong(64, '0');
    check(mgr.loadTrusted(utf8(std::filesystem::path(plain).parent_path()), {wrong}) == 0,
          "wrong SHA-256 loads nothing");
    check(mgr.lastError().empty(), "wrong SHA-256 does not open a library");
    check(!moduleMapped(plain), "untrusted file is not mapped");

    check(mgr.loadTrusted(utf8(std::filesystem::path(plain).parent_path()), {hash}) == 0,
          "matching hash of a non-module does not count as loaded");
    const std::string opened = mgr.lastError();
    check(opened.find("dlopen") != std::string::npos ||
              opened.find("LoadLibrary") != std::string::npos,
          "matching hash is the explicit map attempt: " + opened);
}

static void testSymlinkAndDirectory(PluginManager &mgr, const std::filesystem::path &dir,
                                    const std::filesystem::path &plain,
                                    const std::string &examplePath)
{
    const std::filesystem::path link = dir / ("link" + pluginExt());
    std::error_code ec;
    std::filesystem::create_symlink(mviewer::core::pathFromUtf8(examplePath), link, ec);
    if (ec)
    {
#ifndef _WIN32
        check(false, "create_symlink failed: " + ec.message());
#else
        std::cout << "SKIP symlink fixture: " << ec.message() << '\n';
#endif
    }
    else
    {
        const auto found = PluginManager::scanDirectory(utf8(dir));
        bool sawLink = false;
        for (const auto &candidate : found)
        {
            if (candidate.find("link" + pluginExt()) != std::string::npos)
                sawLink = true;
        }
        check(!sawLink, "scan omits a symlink");
        check(!mgr.load(utf8(link)), "explicit load rejects a symlink");
        check(mgr.lastError().find("symlink") != std::string::npos,
              "symlink error names the policy: " + mgr.lastError());
        check(!containsId("example.mean_luminance"), "symlink target was not registered");
        check(!moduleMapped(examplePath), "symlink target was not mapped");
    }

    const std::filesystem::path nested = dir / ("nested" + pluginExt());
    std::filesystem::create_directory(nested, ec);
    const auto found = PluginManager::scanDirectory(utf8(dir));
    bool sawPlain = false;
    bool sawNested = false;
    for (const auto &candidate : found)
    {
        if (candidate.find("plain" + pluginExt()) != std::string::npos)
            sawPlain = true;
        if (candidate.find("nested" + pluginExt()) != std::string::npos)
            sawNested = true;
    }
    check(sawPlain, "scan keeps a regular file");
    check(!sawNested, "scan omits a directory");

    const std::filesystem::path other = dir / "other-real";
    std::filesystem::create_directories(other, ec);
    const mviewer::core::PluginFileAssessment escaped =
        mviewer::core::assessPluginFile(utf8(plain), utf8(other));
    check(!escaped.regularFile, "a file is not trusted as a child of another directory");
    check(escaped.error.find("escapes") != std::string::npos,
          "canonical parent mismatch is rejected: " + escaped.error);

    const std::filesystem::path dirLink = dir / "dir-link";
    std::filesystem::create_directory_symlink(dir, dirLink, ec);
    if (!ec)
        check(PluginManager::scanDirectory(utf8(dirLink)).empty(),
              "scan refuses a directory symlink");
#ifndef _WIN32
    else
        check(false, "create_directory_symlink failed: " + ec.message());
#else
    else
        std::cout << "SKIP directory symlink fixture: " << ec.message() << '\n';
#endif

    check(mgr.loadDirectory(utf8(dir)) == 0, "loadDirectory does not map a directory");
    check(mgr.lastError().find("untrusted") != std::string::npos,
          "loadDirectory explains that it did not map modules");
    check(!moduleMapped(utf8(plain)), "directory load left the regular file unmapped");
}

static void testExplicitLoadAndUnload(PluginManager &mgr, const std::string &examplePath)
{
    check(mgr.load(examplePath),
          "explicit load of a regular plugin still works: " + mgr.lastError());
    check(containsId("example.mean_luminance"), "explicit load registers the built example");
    check(mgr.unload(examplePath), "unload drops the example registration");
    check(!containsId("example.mean_luminance"), "example id is gone after unload");
    check(mgr.count() == 0 || !mgr.isLoaded(examplePath), "unload forgets the path");
    check(moduleMapped(examplePath), "unload leaves the module mapped for the process lifetime");
}

static void testBoundary(PluginManager &mgr, const std::filesystem::path &bin,
                         const std::filesystem::path &markerPath)
{
    const std::filesystem::path source = bin / ("audit_boundary" + pluginExt());
    struct Case
    {
        const char *mode;
        const char *needle;
        bool create;
        bool destroy;
        bool dtor;
    };
    const Case cases[] = {
        {"null-abi", "null", false, false, false},   {"null-name", "null", false, false, false},
        {"null-create", "null", true, false, false}, {"throw-create", "threw", true, false, false},
        {"throw-name", "threw", true, true, true},
    };
    for (const Case &item : cases)
    {
        const std::filesystem::path copy =
            bin / (std::string("audit_boundary_") + item.mode + pluginExt());
        std::error_code ec;
        std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        check(!ec, std::string("copy boundary plugin for ") + item.mode + ": " + ec.message());
        if (ec)
            continue;
        std::filesystem::remove(markerPath, ec);
        setEnv("MVIEWER_BOUNDARY_MODE", item.mode);
        setEnv("MVIEWER_BOUNDARY_MARKER", utf8(markerPath));
        const bool loaded = mgr.load(utf8(copy));
        const std::string error = mgr.lastError();
        check(!loaded, std::string(item.mode) + " is rejected");
        check(error.find(item.needle) != std::string::npos,
              std::string(item.mode) + " error mentions " + item.needle + ": " + error);
        check(error.find("sandbox") == std::string::npos,
              std::string(item.mode) + " error does not call the catch a sandbox");
        check(!containsId("audit.boundary"), std::string(item.mode) + " is not registered");
        const std::string marker = readText(markerPath);
        check(markerHas(marker, "createAnalyzer") == item.create,
              std::string(item.mode) + " createAnalyzer marker: " + marker);
        check(markerHas(marker, "destroyAnalyzer") == item.destroy,
              std::string(item.mode) + " destroyAnalyzer marker: " + marker);
        check(markerHas(marker, "dtor") == item.dtor,
              std::string(item.mode) + " dtor marker: " + marker);
        std::filesystem::remove(copy, ec);
    }
}

static void testDiscoverCancelThenTrust(PluginManager &mgr, const std::filesystem::path &bin,
                                        const std::filesystem::path &dir)
{
    const std::filesystem::path source = bin / ("audit_boundary" + pluginExt());
    const std::filesystem::path discovered = dir / ("discover" + pluginExt());
    std::error_code ec;
    std::filesystem::copy_file(source, discovered,
                               std::filesystem::copy_options::overwrite_existing, ec);
    check(!ec, "copy plugin for discovery: " + ec.message());
    if (ec)
        return;

    const std::filesystem::path marker = dir / "ctor-marker.txt";
    std::filesystem::remove(marker, ec);
    setEnv("MVIEWER_BOUNDARY_MODE", "ok");
    setEnv("MVIEWER_BOUNDARY_MARKER", utf8(marker));

    const auto found = PluginManager::scanDirectory(utf8(dir));
    bool saw = false;
    for (const auto &candidate : found)
    {
        if (candidate.find("discover" + pluginExt()) != std::string::npos)
            saw = true;
    }
    check(saw, "discovery lists the plugin file");
    std::string hashError;
    const std::string hash = mviewer::core::pluginFileSha256(utf8(discovered), hashError);
    check(mviewer::core::isPluginSha256Hex(hash), "discovery hashes without mapping");
    check(!moduleMapped(utf8(discovered)), "discovery does not map the plugin");
    check(!markerHas(readText(marker), "ctor"), "discovery leaves the constructor unexecuted");

    ExplicitPluginTrust identified;
    identified.path = utf8(discovered);
    identified.sha256Hex = hash;
    const std::string prompt = pluginTrustPromptBody({identified});
    check(prompt.find(identified.path) != std::string::npos, "confirmation names the path");
    check(prompt.find(hash) != std::string::npos, "confirmation names the SHA-256");
    check(prompt.find("inside this process") != std::string::npos,
          "confirmation warns that native code runs in-process");
    check(prompt.find("not a sandbox") != std::string::npos,
          "confirmation does not call the load a sandbox");

    const auto cancelled = mgr.commitExplicitTrust({identified}, false);
    check(cancelled.size() == 1 && !cancelled[0].loaded && !cancelled[0].persist,
          "cancel does not map or persist");
    check(!moduleMapped(utf8(discovered)), "cancel leaves the plugin unmapped");
    check(!markerHas(readText(marker), "ctor"), "cancel leaves the constructor unexecuted");
    check(!containsId("audit.boundary"), "cancel does not register the plugin");

    // Load the confirmed copy from the executable directory so its dependency
    // on mviewer_core resolves the same way as the other plugin tests.
    const std::filesystem::path trusted = bin / ("audit_trust_yes" + pluginExt());
    std::filesystem::copy_file(source, trusted, std::filesystem::copy_options::overwrite_existing,
                               ec);
    check(!ec, "copy plugin for explicit trust: " + ec.message());
    if (ec)
        return;
    std::filesystem::remove(marker, ec);
    const std::string trustedHash = mviewer::core::pluginFileSha256(utf8(trusted), hashError);
    ExplicitPluginTrust grant;
    grant.path = utf8(trusted);
    grant.sha256Hex = trustedHash;
    const auto declined = mgr.commitExplicitTrust({grant}, false);
    check(!declined.empty() && !declined[0].loaded, "a second cancel still does not map");
    check(!markerHas(readText(marker), "ctor"), "declined trust leaves the constructor unexecuted");

    const auto granted = mgr.commitExplicitTrust({grant}, true);
    check(granted.size() == 1 && granted[0].loaded && granted[0].persist,
          "explicit trust maps the confirmed bytes and may persist that hash");
    check(granted[0].sha256Hex == trustedHash, "persisted hash is the confirmed hash");
    check(containsId("audit.boundary"), "explicit trust executes and registers the plugin");
    check(markerHas(readText(marker), "ctor"), "explicit trust runs the constructor");

    ExplicitPluginTrust replacement;
    replacement.path = grant.path;
    replacement.sha256Hex = std::string(64, 'a');
    const auto rebound = mgr.commitExplicitTrust({replacement}, true);
    check(!rebound.empty() && !rebound[0].loaded && !rebound[0].persist,
          "a loaded path is not rebound to different bytes");
    const auto again = mgr.commitExplicitTrust({grant}, true);
    check(!again.empty() && !again[0].loaded && !again[0].persist,
          "a loaded path does not store its hash a second time");

    mgr.unload(grant.path);
    std::filesystem::remove(trusted, ec);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    std::cout << "[plugin trust tests]\n";
    auto &mgr = PluginManager::instance();
    const std::filesystem::path bin = exeDir(argc > 0 ? argv[0] : nullptr);
    const std::string example = utf8(bin / ("example_analyzer" + pluginExt()));

    const auto stamp = std::to_string(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        static_cast<long long>(getpid())
#endif
    );
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("mviewer-plugin-trust-" + stamp);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    check(!ec, "create temp plugin dir");
    const std::filesystem::path plain = dir / ("plain" + pluginExt());
    {
        std::ofstream out(plain, std::ios::binary);
        out << "not-a-native-module\n";
    }

    testHashDoesNotMap(mgr, utf8(plain));
    testSymlinkAndDirectory(mgr, dir, plain, example);
    testExplicitLoadAndUnload(mgr, example);
    testBoundary(mgr, bin, dir / "boundary-marker.txt");
    testDiscoverCancelThenTrust(mgr, bin, dir);

    std::filesystem::remove_all(dir, ec);
    if (g_fail)
    {
        std::cerr << g_fail << " FAILED\n";
        return 1;
    }
    std::cout << "plugin_trust_tests: all passed\n";
    return 0;
}
