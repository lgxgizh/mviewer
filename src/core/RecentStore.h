//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// RecentStore — recently-viewed path list (review P1-1, step 2a). Split out of
// RatingStore so the filter keeps working after rating/label/flag/tag UI is
// gone. One UTF-8 path per line in recents.txt, most recent first.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace mviewer::core
{

class RecentStore
{
  public:
    static constexpr int kMaxRecents = 200;

    static RecentStore &instance();

    // Move to front, dedupe by normalizeKey, cap at kMaxRecents. Marks dirty
    // and schedules one background save.
    void addRecent(const std::string &path);

    // Most recent first, normalized keys.
    std::vector<std::string> recents() const;
    bool isRecent(const std::string &path) const;

    // Replace the stored key in place. Position is unchanged.
    void renamePath(const std::string &oldPath, const std::string &newPath);

    // Drop entries whose file no longer exists. Returns how many were removed.
    size_t pruneMissing();

    // Synchronous write of the current list when dirty.
    bool flush();

    // Test hook. Loads recents.txt at `path`. Migration reads flags.txt beside it.
    void setFilePath(const std::string &path);

  private:
    RecentStore() = default;

    std::string defaultPath() const;
    void ensureLoaded() const;
    void loadLocked() const;
    std::string serializeLocked() const;
    void markDirty();
    void enqueueSave();
    void backgroundSave();
    bool writeDirty();

    mutable std::mutex m_mutex;
    // Serializes file writes (background save vs. shutdown flush) so flush()
    // returns only after any in-flight write has finished.
    std::mutex m_writeMutex;
    mutable std::vector<std::string> m_recents;
    mutable std::string m_filePath;
    mutable bool m_loaded = false;
    mutable bool m_dirty = false;
    mutable bool m_saveQueued = false;
    mutable std::uint64_t m_epoch = 0;
};

} // namespace mviewer::core
