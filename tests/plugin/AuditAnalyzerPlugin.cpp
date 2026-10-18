// Test plugin for probe lifetime, destroyAnalyzer, and the legacy ABI gate.
// Built twice: audit_analyzer (compatible ABI) and audit_legacy_mismatch
// (legacy version export only, deliberately wrong).
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/MViewerPluginExport.h"
#include "core/plugin/PluginABI.h"
#include "domain/Selection.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace
{

int g_creates = 0;
int g_destroys = 0;
int g_live = 0;

void markSymbolUsed(const char *symbol)
{
#ifdef AUDIT_PLUGIN_LEGACY_MISMATCH
    if (const char *path = std::getenv("MVIEWER_AUDIT_SYMBOL_MARKER"))
    {
        std::ofstream out(path, std::ios::app);
        if (out)
            out << symbol << '\n';
    }
#else
    (void)symbol;
#endif
}

class AuditAnalyzer : public Analyzer
{
  public:
    AuditAnalyzer()
    {
        ++g_live;
    }
    ~AuditAnalyzer() override
    {
        --g_live;
    }

    std::string name() const override
    {
#ifdef AUDIT_PLUGIN_LEGACY_MISMATCH
        return "audit.legacy";
#else
        return "audit.probe";
#endif
    }
    std::string description() const override
    {
        return "Audit probe analyzer";
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

    MVIEWER_PLUGIN_EXPORT Analyzer *createAnalyzer()
    {
        markSymbolUsed("createAnalyzer");
        ++g_creates;
        return new AuditAnalyzer();
    }

    MVIEWER_PLUGIN_EXPORT void destroyAnalyzer(Analyzer *analyzer)
    {
        markSymbolUsed("destroyAnalyzer");
        ++g_destroys;
        delete analyzer;
    }

    MVIEWER_PLUGIN_EXPORT const char *pluginName()
    {
        markSymbolUsed("pluginName");
#ifdef AUDIT_PLUGIN_LEGACY_MISMATCH
        return "audit.legacy";
#else
        return "audit.probe";
#endif
    }

    MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version()
    {
#ifdef AUDIT_PLUGIN_LEGACY_MISMATCH
        return 999;
#else
        return MVIEWER_API_VERSION;
#endif
    }

#ifndef AUDIT_PLUGIN_LEGACY_MISMATCH
    MVIEWER_PLUGIN_EXPORT const PluginABI *mviewer_plugin_abi()
    {
        static const PluginABI abi;
        return &abi;
    }
#endif

    MVIEWER_PLUGIN_EXPORT int auditAnalyzerCreateCalls()
    {
        return g_creates;
    }

    MVIEWER_PLUGIN_EXPORT int auditAnalyzerDestroyCalls()
    {
        return g_destroys;
    }

    MVIEWER_PLUGIN_EXPORT int auditAnalyzerLive()
    {
        return g_live;
    }

} // extern "C"
