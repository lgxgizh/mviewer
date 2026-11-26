// Maps an already authorized regular file. Mapping runs DllMain or .init_array
// before any ABI export can be read. Callers reject symlinks and untrusted
// files first. This is not a trust check.
//
// Review P2-1: one hardened entry point for loading plugin shared libraries.
//
// Windows: LoadLibraryExW with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
// LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32, so a
// plugin's own dependencies resolve from the plugin folder, the MViewer folder
// and System32 only, never from the current directory or PATH (DLL planting).
// Other platforms: dlopen(RTLD_NOW | RTLD_LOCAL).
#pragma once

#include <string>

namespace mviewer::core
{

// Returns the native module handle, or nullptr with `error` set.
void *loadPluginLibrary(const std::string &utf8Path, std::string &error);

// Same mapping, but `expectedSha256Hex` (lowercase) must still match the bytes on
// disk at map time. On Windows the file is opened read-only with write sharing
// denied and kept open across LoadLibraryExW, so a caller who just hashed it
// cannot be raced into mapping a different file. Without a verified load there
// is a window between "hash matched" and "module mapped" in which the file can
// be replaced - and DllMain runs at map time.
void *loadPluginLibraryVerified(const std::string &utf8Path, const std::string &expectedSha256Hex,
                                std::string &error);

// True when the module exports neither the ABI descriptor
// (mviewer_plugin_abi) nor the legacy version (mviewer_plugin_api_version).
// Such modules are rejected: the host cannot tell what contract they follow.
constexpr const char *kPluginMissingAbiError =
    "plugin exports neither mviewer_plugin_abi nor mviewer_plugin_api_version";

} // namespace mviewer::core
