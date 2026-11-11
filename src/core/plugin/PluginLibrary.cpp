#include "core/plugin/PluginLibrary.h"

#include "core/filesystem/Utf8Path.h"

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
    void *handle = dlopen(utf8Path.c_str(), RTLD_LAZY);
    if (!handle)
    {
        const char *reason = dlerror();
        error = "dlopen failed: " + std::string(reason ? reason : "unknown");
    }
    return handle;
#endif
}

} // namespace mviewer::core
