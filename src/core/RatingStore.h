//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// RatingStore — persistent 0-5 star rating index for image files.
// Domain-free core (no Qt). Used by the thumbnail gallery, metadata panel
// and compare workflow. Persisted as a plain text file keyed by absolute
// path so it stays independent of the thumbnail cache.
//
#pragma once

#include <map>
#include <mutex>
#include <string>

namespace mviewer::core {

class RatingStore
{
public:
    // Process-wide singleton backed by a platform data directory.
    static RatingStore& instance();

    // 0 = unrated; 1..5 = stars.
    int rating(const std::string& path) const;
    bool hasRating(const std::string& path) const;
    void setRating(const std::string& path, int stars);
    void clearRating(const std::string& path);

    // Persistence. save()/load() use the current file path.
    bool save() const;
    bool load();
    void setFilePath(const std::string& path);

private:
    RatingStore();
    std::string defaultPath() const;
    std::string normalize(const std::string& path) const;

    mutable std::mutex m_mutex;
    std::map<std::string, int> m_ratings;
    std::string m_filePath;
};

}  // namespace mviewer::core
