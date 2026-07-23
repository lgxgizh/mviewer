#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/export/IExporter.h"
#include "core/image/ImageBuffer.h"

// Registry of available image exporters. Populated by static registration
// (built-in exporters) and by PluginManager when an Exporter plugin is loaded.
// Mirrors DecoderRegistry; holds shared_ptr so plugin-provided instances are
// deleted inside the plugin module that created them.
class ExporterRegistry
{
  public:
    static ExporterRegistry &instance();

    void registerExporter(std::shared_ptr<IExporter> exporter);
    void unregister(const std::string &id);

    std::shared_ptr<IExporter> get(const std::string &id) const;
    std::vector<std::string> available() const;
    std::vector<std::string> supportedExtensions() const;

    // Export img to outPath using the exporter registered under id.
    // Returns false if the id is unknown or the exporter fails.
    bool exportImage(const std::string &id, const ImageData &img, const std::string &outPath) const;

  private:
    ExporterRegistry() = default;
    std::vector<std::shared_ptr<IExporter>> m_exporters;
};
