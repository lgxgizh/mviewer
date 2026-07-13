#pragma once

#include "core/image/ImageBuffer.h"

#include <string>
#include <set>

// SQLite-backed disk cache for decoded full-resolution images.
// Key = file identity hash (path + mtime + size), so mtime change
// automatically invalidates the entry.
class DiskCache
{
public:
    static DiskCache &instance();

    // Retrieve decoded image from disk cache. Returns false on miss.
    bool get(const std::string &key, ImageData &out);

    // Store decoded image to disk cache.
    void put(const std::string &key, const ImageData &img);

    // Remove a single entry (used by repository invalidate).
    void remove(const std::string &key);

    // Number of cached entries.
    size_t entryCount() const;

    // Soft cap on entry count; enforced lazily on put() by dropping oldest.
    void setMaxEntries(int n) { m_maxEntries = n; }
    int maxEntries() const { return m_maxEntries; }

    // Clear all cached entries.
    void clear();

    // Remove stale entries for paths no longer present.
    void prune(const std::set<std::string> &validKeys);

    // Enable/disable disk caching (default: enabled)
    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const { return m_enabled; }

private:
    DiskCache();
    ~DiskCache();
    DiskCache(const DiskCache &) = delete;
    void operator=(const DiskCache &) = delete;

    void ensureTable();
    void openDb();

    class Impl; // hides Qt SQL headers from this header
    Impl *m_impl = nullptr;
    bool m_enabled = true;
    std::string m_dbPath;
    int m_maxEntries = 100000;
};
