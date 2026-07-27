//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// TagStore — persistent, free-form tag index for image files (review P0:
// "Filter ... Tag"). Domain-free core (no Qt). Persisted as a plain text file
// keyed by absolute path so it stays independent of the thumbnail cache.
//
//   tags.txt : "<tag>|<path>"   (one line per tag assignment)
//
#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace mviewer::core
{

class TagStore
{
  public:
    // Process-wide singleton backed by a platform data directory.
    static TagStore &instance();

    // Tags assigned to a path, sorted for stable display.
    std::vector<std::string> tags(const std::string &path) const;
    bool hasTag(const std::string &path, const std::string &tag) const;
    void addTag(const std::string &path, const std::string &tag);
    void removeTag(const std::string &path, const std::string &tag);
    void setTags(const std::string &path, const std::vector<std::string> &tags);
    void clearTags(const std::string &path);

    // Every distinct tag in the store, sorted.
    std::vector<std::string> allTags() const;

    bool save() const;
    bool load();
    void setFilePath(const std::string &path);

  private:
    TagStore();
    std::string defaultPath() const;
    std::string normalize(const std::string &path) const;

    mutable std::mutex m_mutex;
    std::map<std::string, std::set<std::string>> m_tags; // path -> tags
    std::string m_filePath;
};

} // namespace mviewer::core
