#pragma once

#include "core/import/IImporter.h"

#include <memory>
#include <string>
#include <vector>

// Registry of available workspace importers. Populated by static registration
// (built-in importers) and by PluginManager when an Importer plugin is loaded.
// Mirrors ExporterRegistry / DecoderRegistry.
class ImporterRegistry
{
  public:
    static ImporterRegistry &instance();

    void registerImporter(std::shared_ptr<IImporter> importer);
    void unregister(const std::string &id);

    std::shared_ptr<IImporter> get(const std::string &id) const;
    std::vector<std::string> available() const;

    // Find the first importer that canImport(path), or nullptr.
    std::shared_ptr<IImporter> findFor(const std::string &path) const;

    // Import via a specific id, or auto-detect via findFor. Returns empty on fail.
    mviewer::domain::Workspace importWorkspace(const std::string &path,
                                               const std::string &id = {}) const;

  private:
    ImporterRegistry() = default;
    std::vector<std::shared_ptr<IImporter>> m_importers;
};
