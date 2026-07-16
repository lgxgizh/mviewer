#pragma once

#include "core/image/ImageBuffer.h"

#include <mutex>
#include <set>
#include <string>

class QSqlDatabase; // forward decl: connectionForThread() returns one; the real
                     // Qt SQL header stays hidden behind the Impl PIMPL.

// SQLite-backed disk cache for decoded full-resolution images.
// Key = file identity hash (path + mtime + size), so mtime change
// automatically invalidates the entry.
class DiskCache
{
public:
    static DiskCache& instance();

    // Retrieve decoded image from disk cache. Returns false on miss.
    bool get(const std::string& key, ImageData& out);

    // Store decoded image to disk cache.
    void put(const std::string& key, const ImageData& img);

    // Remove a single entry (used by repository invalidate).
    void remove(const std::string& key);

    // Number of cached entries.
    size_t entryCount() const;

    // Total bytes consumed by all cached blobs (approximate).
    size_t totalBytes() const;

    // Soft cap on entry count; enforced lazily on put() by dropping oldest.
    void setMaxEntries(int n) { m_maxEntries = n; }
    int maxEntries() const { return m_maxEntries; }

    // Soft cap on total bytes consumed; enforced lazily on put() by dropping
    // oldest entries until totalBytes() falls below this value.
    void setMaxBytes(size_t n) { m_maxBytes = n; }
    size_t maxBytes() const { return m_maxBytes; }

    // Clear all cached entries.
    void clear();

    // Remove stale entries for paths no longer present.
    void prune(const std::set<std::string>& validKeys);

    // Enable/disable disk caching (default: enabled)
    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const { return m_enabled; }

private:
    DiskCache();
    ~DiskCache();
    DiskCache(const DiskCache&) = delete;
    void operator=(const DiskCache&) = delete;

    void ensureTable();
    void openDb();

    // Returns a QSqlDatabase connection owned by the *current* thread, opened
    // against the same SQLite file as the main-thread template connection.
    // QSqlDatabase is thread-bound: a connection created on one thread must not
    // be touched from another. This gives every TaskScheduler worker thread its
    // own connection so parallel load() calls stop sharing (and corrupting) the
    // main-thread connection. The m_mutex still serializes SQL statements.
    QSqlDatabase connectionForThread() const;

    class Impl; // hides Qt SQL headers from this header
    Impl* m_impl = nullptr;

    // Guards all access to the shared QSqlDatabase connection. QSqlDatabase /
    // QSqlQuery are NOT thread-safe: a single connection must not be used
    // concurrently from multiple threads. DiskCache::instance() is a singleton
    // hit by every parallel load() in ImageRepository::loadDirectory, so every
    // DB-touching method must serialize on this mutex. Without it, concurrent
    // put()/get() calls race and corrupt Qt's connection state and the C++ heap
    // (observed as STATUS_HEAP_CORRUPTION / 0xC0000374 during the 1000-image
    // loadDirectory test).
    // Uses std::recursive_mutex (NOT a Qt type) to keep this core header Qt-free
    // per AGENTS.md; the .cpp still uses QMutex/QMutexLocker for the per-thread
    // connection-creation lock where Qt is allowed.
    mutable std::recursive_mutex m_mutex;
    bool m_enabled = true;
    std::string m_dbPath;
    int m_maxEntries = 100000;
    size_t m_maxBytes = 2147483648ULL; // 2 GB default cap
};
