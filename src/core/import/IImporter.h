#pragma once

#include "domain/Workspace.h"

#include <string>
#include <vector>

// Core import interface (Qt-free header). Mirrors IExporter / IDecoder as a
// plugin-discoverable extension point so third-party importers can register
// into ImporterRegistry through the same PluginLoader contract.
//
// An importer turns an external catalog / project / folder layout into a
// domain Workspace (folders + image metadata, no pixels).
class IImporter
{
  public:
    virtual ~IImporter() = default;

    // Stable, unique id used for registration and lookup (e.g. "folder-importer").
    virtual std::string name() const = 0;
    // Human-readable description shown in UI.
    virtual std::string description() const = 0;
    // Input extensions this importer can read, lowercased without dot
    // (e.g. {"mvws", "json"}). Empty means "any path / directory".
    virtual std::vector<std::string> extensions() const = 0;

    // True if this importer can handle the given path (file or directory).
    virtual bool canImport(const std::string &path) const = 0;

    // Import path into a Workspace. Returns empty Workspace on failure.
    virtual mviewer::domain::Workspace importWorkspace(const std::string &path) const = 0;
};
