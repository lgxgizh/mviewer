#include "core/plugin/PluginLibrary.h"

#include "core/filesystem/Utf8Path.h"
#include "core/plugin/PluginPathPolicy.h"

#include <QCryptographicHash>

#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mviewer::core
{

void *loadPluginLibrary(const std::string &utf8Path, std::string &error)
{
#ifdef _WIN32
    const std::filesystem::path nativePath = pathFromUtf8(utf8Path);
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR requires an absolute path.
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(nativePath, ec);
    const std::wstring &wide = ec ? nativePath.native() : absolute.native();
    constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                             LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, kFlags);
    if (!module)
        error = "LoadLibrary failed: " + std::to_string(GetLastError());
    return reinterpret_cast<void *>(module);
#else
    // RTLD_NOW: resolve every symbol up front so a broken plugin fails at load
    // time instead of crashing later at a call site. RTLD_LOCAL: the plugin's
    // symbols must not leak into the host's dynamic symbol table.
    void *handle = dlopen(utf8Path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char *reason = dlerror();
        error = "dlopen failed: " + std::string(reason ? reason : "unknown");
    }
    return handle;
#endif
}

void *loadPluginLibraryVerified(const std::string &utf8Path, const std::string &expectedSha256Hex,
                                std::string &error)
{
    error.clear();
    if (expectedSha256Hex.empty())
        return loadPluginLibrary(utf8Path, error);

#ifdef _WIN32
    const std::filesystem::path nativePath = pathFromUtf8(utf8Path);
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(nativePath, ec);
    const std::wstring &wide = ec ? nativePath.native() : absolute.native();
    constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                             LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;

    // Deny write and delete sharing: while this handle is open nothing can
    // truncate or replace the bytes we are about to hash and map.
    const HANDLE file =
        CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "plugin file cannot be opened for verification: " +
                std::to_string(GetLastError());
        return nullptr;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    unsigned char buffer[64 * 1024];
    DWORD read = 0;
    for (;;)
    {
        const BOOL ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
        if (!ok)
        {
            // Distinguish an I/O failure from a clean EOF: hashing a half-read
            // file and reporting it as "changed on disk" sends the user after a
            // ghost.
            error = "plugin file could not be read: " + std::to_string(GetLastError());
            CloseHandle(file);
            return nullptr;
        }
        if (read == 0)
            break; // EOF
        hash.addData(reinterpret_cast<const char *>(buffer), static_cast<qsizetype>(read));
    }

    const std::string actual = hash.result().toHex().toStdString();
    bool matches = true;
    if (actual.size() != expectedSha256Hex.size())
        matches = false;
    else
        for (size_t i = 0; i < actual.size(); ++i)
            matches = matches && (actual[i] == expectedSha256Hex[i]);

    if (!matches)
    {
        CloseHandle(file);
        error = "plugin changed on disk before it could be mapped";
        return nullptr;
    }

    const HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, kFlags);
    CloseHandle(file); // the mapping keeps its own reference to the file
    if (!module)
        error = "LoadLibrary failed: " + std::to_string(GetLastError());
    return reinterpret_cast<void *>(module);
#else
    // POSIX has no equivalent of a share-restricted handle around dlopen; fall
    // back to hash-then-load and report the residual window rather than pretending
    // it is closed.
    std::string hashError;
    const std::string actual = pluginFileSha256(utf8Path, hashError);
    if (actual != expectedSha256Hex)
    {
        error = actual.empty() ? hashError : "plugin changed on disk before it could be mapped";
        return nullptr;
    }
    return loadPluginLibrary(utf8Path, error);
#endif
}

} // namespace mviewer::core
