#pragma once

#include <cstdint>

// M14-5: Plugin ABI version. Host and plugin must agree on this version.
// Bump when the plugin API (createAnalyzer/destroyAnalyzer signatures,
// AnalyzerCapability flags, or other ABI) changes incompatibly.
#define MVIEWER_PLUGIN_API_VERSION 1

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

// Every MViewer plugin MUST export this function:
//   extern "C" int mviewer_plugin_api_version();
// returning MVIEWER_PLUGIN_API_VERSION. Host checks this before use.
// Optional: extern "C" uint32_t mviewer_plugin_capabilities();
