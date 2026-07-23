#pragma once

#include <cstdint>
#include <string>

// ── M14.2: Frozen Plugin ABI version triple ─────────────────────────────────
//
// Every MViewer plugin exports a descriptor via:
//   extern "C" const PluginABI* mviewer_plugin_abi();
// The host compares it against its own ABI before loading. Full contract and
// bump policy live in docs/sdk/PLUGIN_ABI.md.
//
//   apiVersion  — the plugin *API contract* (Analyzer/Decoder/Exporter
//                 interfaces, exported C symbols, capability flags). Bump on an
//                 incompatible API change. Plugins with apiVersion <= host are
//                 accepted (backward compatible).
//   abiVersion  — the *binary* ABI compatibility level (struct layouts, calling
//                 convention, std-lib boundary, compiler/Qt build). MUST match
//                 the host exactly. A mismatch means the plugin must be
//                 recompiled. For the entire v1.x line this stays at 1, so v1.x
//                 plugins never need recompiling.
//   sdkVersion  — the SDK *release* the plugin was built against, encoded as
//                 major*10000 + minor*100 + patch. Informational only; a
//                 mismatch is a warning, never a hard gate.

#define MVIEWER_API_VERSION 1     // plugin API contract version
#define MVIEWER_ABI_VERSION 1     // binary ABI compatibility level (v1.x == 1)
#define MVIEWER_SDK_VERSION 10000 // SDK release 1.0.0

// Backward-compatible alias for the single-version export still used by some
// plugins / the loader's legacy fallback.
#define MVIEWER_PLUGIN_API_VERSION MVIEWER_API_VERSION

struct PluginABI
{
    uint32_t apiVersion = MVIEWER_API_VERSION;
    uint32_t abiVersion = MVIEWER_ABI_VERSION;
    uint32_t sdkVersion = MVIEWER_SDK_VERSION;
};

// Host ABI descriptor — the version this build of MViewer exposes.
inline const PluginABI &hostPluginABI()
{
    static const PluginABI abi;
    return abi;
}

// Hard compatibility gate: may the host load this plugin?
//   * abiVersion must match exactly (else the plugin must be recompiled).
//   * plugin apiVersion must not be newer than the host (else unsupported API).
inline bool pluginABICompatible(const PluginABI &host, const PluginABI &plugin)
{
    if (host.abiVersion != plugin.abiVersion)
        return false;
    if (plugin.apiVersion > host.apiVersion)
        return false;
    return true;
}

// Informational warnings — never block loading. Returns "" when fully aligned.
inline std::string pluginABIWarnings(const PluginABI &host, const PluginABI &plugin)
{
    if (plugin.sdkVersion > host.sdkVersion)
        return "plugin SDK (" + std::to_string(plugin.sdkVersion) + ") is newer than host SDK (" +
               std::to_string(host.sdkVersion) + ")";
    if (plugin.sdkVersion < host.sdkVersion)
        return "plugin SDK (" + std::to_string(plugin.sdkVersion) + ") is older than host SDK (" +
               std::to_string(host.sdkVersion) + ")";
    return {};
}

// Capability flags plugins can declare. Host queries these to know what
// the plugin supports (single image, region, batch, RAW).
enum class PluginCapability : uint32_t
{
    None = 0,
    SingleImage = 1 << 0,
    Region = 1 << 1,
    Batch = 1 << 2,
    RAW = 1 << 3,
};

inline PluginCapability operator|(PluginCapability a, PluginCapability b)
{
    return static_cast<PluginCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasCapability(PluginCapability flags, PluginCapability cap)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(cap)) != 0;
}

// Every MViewer plugin MUST export (M14.2):
//   extern "C" const PluginABI* mviewer_plugin_abi();
// Legacy single-version export (kept for backward compatibility):
//   extern "C" int mviewer_plugin_api_version();  // -> MVIEWER_API_VERSION
// Optional:
//   extern "C" uint32_t mviewer_plugin_capabilities();
