//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// RatingStore — persistent 0-5 star rating index for image files, plus the
// review's P3 "rating tail": color labels, reject/pick flags and a recents
// (recently-viewed) list. Domain-free core (no Qt). Persisted as plain text
// files keyed by absolute path so it stays independent of the thumbnail cache.
//
//   ratings.txt : "<stars>|<path>"          (star rating only)
//   flags.txt   : "L|<path>|<n>" label, "X|<path>" rejected,
//                 "K|<path>" picked, "N|<path>" recent (in order)
//
#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace mviewer::core
{

class RatingStore
{
  public:
    // Process-wide singleton backed by a platform data directory.
    static RatingStore &instance();

    // ---- star rating: 0 = unrated; 1..5 = stars ----
    int rating(const std::string &path) const;
    bool hasRating(const std::string &path) const;
    void setRating(const std::string &path, int stars);
    void clearRating(const std::string &path);

    // ---- color label: 0 = none, 1..6 = red/orange/yellow/green/blue/purple ----
    int colorLabel(const std::string &path) const;
    bool hasColorLabel(const std::string &path) const;
    void setColorLabel(const std::string &path, int label);
    void clearColorLabel(const std::string &path);

    // ---- reject / pick (favorite) ----
    bool rejected(const std::string &path) const;
    void setRejected(const std::string &path, bool v);
    bool picked(const std::string &path) const;
    void setPicked(const std::string &path, bool v);

    // ---- recents (most-recently-viewed first), capped ----
    std::vector<std::string> recents() const;
    void addRecent(const std::string &path);
    static constexpr int kMaxRecents = 200;

    // ---- favorites == picked set ----
    std::vector<std::string> favorites() const;

    // Persistence. save()/load() use the current file path.
    bool save() const;
    bool load();
    void setFilePath(const std::string &path);

  private:
    RatingStore();
    std::string defaultPath() const;
    std::string flagsPath() const;
    std::string normalize(const std::string &path) const;

    void saveFlags() const;
    void loadFlags();

    mutable std::mutex m_mutex;
    std::map<std::string, int> m_ratings;
    std::map<std::string, int> m_colorLabels; // 0..6
    std::set<std::string> m_rejected;
    std::set<std::string> m_picked;
    std::vector<std::string> m_recents; // most recent first
    std::string m_filePath;
    std::string m_flagsPath;
};

} // namespace mviewer::core
