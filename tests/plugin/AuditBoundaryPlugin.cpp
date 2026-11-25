// Test plugin for null exports and C++ exceptions at the ABI boundary.
// The mode is read when a callback runs, not from a static constructor.
// Static initialization must stay trivial: mapping this file is not the test.
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/MViewerPluginExport.h"
#include "core/plugin/PluginABI.h"
#include "domain/Selection.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{

int g_live = 0;

std::string mode()
{
    const char *value = std::getenv("MVIEWER_BOUNDARY_MODE");
    return value ? std::string(value) : std::string();
}

void mark(const char *symbol)
{
    const char *path = std::getenv("MVIEWER_BOUNDARY_MARKER");
    if (!path)
        return;
    std::ofstream out(path, std::ios::app);
    if (out)
        out << symbol << '\n';
}

class BoundaryAnalyzer : public Analyzer
{
  public:
    BoundaryAnalyzer()
    {
        ++g_live;
        mark("ctor");
    }
    ~BoundaryAnalyzer() override
    {
        --g_live;
        mark("dtor");
    }
    std::string name() const override
    {
        mark("name");
        if (mode() == "throw-name")
            throw std::runtime_error("boundary-name");
        return "audit.boundary";
    }
    std::string description() const override
    {
        return "ABI boundary fixture";
    }
    bool analyze(const ImageFrame &) override
    {
        return true;
    }
    bool analyzeRegion(const ImageFrame &, const mviewer::domain::Selection &) override
    {
        return true;
    }
};

} // namespace

extern "C"
{

    MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
    {
        mark("mviewer_plugin_abi");
        if (mode() == "null-abi")
            return nullptr;
        static const PluginABI abi;
        return &abi;
    }

    MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version()
    {
        return MVIEWER_API_VERSION;
    }

    MVIEWER_PLUGIN_EXPORT const char *pluginName()
    {
        mark("pluginName");
        if (mode() == "null-name")
            return nullptr;
        return "audit.boundary";
    }

    MVIEWER_PLUGIN_EXPORT Analyzer *createAnalyzer()
    {
        mark("createAnalyzer");
        if (mode() == "throw-create")
            throw std::runtime_error("boundary-create");
        if (mode() == "null-create")
            return nullptr;
        return new BoundaryAnalyzer();
    }

    MVIEWER_PLUGIN_EXPORT void destroyAnalyzer(Analyzer *analyzer)
    {
        mark("destroyAnalyzer");
        delete analyzer;
    }

    MVIEWER_PLUGIN_EXPORT int boundaryLive()
    {
        return g_live;
    }

} // extern "C"
