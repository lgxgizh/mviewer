#pragma once

#include "domain/Workspace.h"

#include <string>
#include <vector>

namespace mviewer::core
{

// Serialize / deserialize a domain::Workspace to a compact JSON document.
// Pure std (no Qt, no external JSON lib) so it stays in the Qt-free core. The
// format is the one emitted by serialize(); deserialize() is tolerant of that
// shape only.
std::string serializeWorkspace(const mviewer::domain::Workspace &ws);
bool deserializeWorkspace(const std::string &text, mviewer::domain::Workspace &out);

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
