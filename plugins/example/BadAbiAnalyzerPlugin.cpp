// M14.2 test-only plugin: deliberately declares a WRONG abiVersion (999) so the
// host's ABI freeze gate rejects it. Proves the gate is enforced end-to-end.
// This plugin is never shipped; it exists only to back test_pluginabi.
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerCapability.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/PluginABI.h"
#include "domain/Selection.h"

#include <cmath>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MVIEWER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
class BadAbiAnalyzer : public Analyzer
{
  public:
    std::string name() const override { return "example.bad_abi"; }
    std::string description() const override { return "Test plugin with a wrong ABI version"; }
    AnalyzerCapability capabilities() const override { return AnalyzerCapability::SingleImage; }
    bool analyze(const ImageFrame &) override { return true; }
    bool analyzeRegion(const ImageFrame &, const mviewer::domain::Selection &) override { return true; }
    std::string resultText() const override { return "bad"; }
};
} // namespace

extern "C"
{
    MVIEWER_PLUGIN_EXPORT Analyzer *createAnalyzer() { return new BadAbiAnalyzer(); }
    MVIEWER_PLUGIN_EXPORT void destroyAnalyzer(Analyzer *a) { delete a; }
    MVIEWER_PLUGIN_EXPORT const char *pluginName() { return "example.bad_abi"; }

    // M14.2: declare a deliberately incompatible ABI triple (abiVersion 999).
    MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
    {
        static const PluginABI abi{MVIEWER_API_VERSION, 999, MVIEWER_SDK_VERSION};
        return &abi;
    }
    MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version() { return MVIEWER_API_VERSION; }
}
