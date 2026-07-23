#pragma once

#include "domain/CompareSession.h"
#include "domain/Workspace.h"

#include <optional>
#include <string>
#include <vector>

namespace mviewer::core
{

// Serialize / deserialize a domain::Workspace to a compact JSON document.
// Pure std (no Qt, no external JSON lib) so it stays in the Qt-free core. The
// format is the one emitted by serialize(); deserialize() is tolerant of that
// shape only. Returns nullopt when the document is malformed or empty.
std::string serializeWorkspace(const mviewer::domain::Workspace &ws);
std::optional<mviewer::domain::Workspace> deserializeWorkspace(const std::string &text);

// M15: CompareSession snapshot embedded in the workspace JSON so the full
// compare view (sync mode, per-cell zoom/pan, shared transform, ROI) survives a
// reload. Pure std; keeps the compare state in the Qt-free core. Returns
// nullopt when the document is malformed.
std::string serializeCompareSession(const mviewer::domain::CompareSession &s);
std::optional<mviewer::domain::CompareSession> deserializeCompareSession(const std::string &text);

// Recent-files list (cheap, high-value product feature). Capped at maxEntries;
// most-recent first; duplicates are moved to front rather than duplicated.
class RecentFiles
{
  public:
    explicit RecentFiles(size_t maxEntries = 10) : m_max(maxEntries)
    {
    }

    void add(const std::string &path);
    const std::vector<std::string> &items() const
    {
        return m_items;
    }
    void clear()
    {
        m_items.clear();
    }

    std::string serialize() const;
    bool deserialize(const std::string &text);

  private:
    size_t m_max;
    std::vector<std::string> m_items;
};

} // namespace mviewer::core
