#pragma once

#include "core/image/ImageBuffer.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <set>
#include <string>

class QSqlDatabase; // forward decl: connectionForThread() returns one; the real
                    // Qt SQL header stays hidden behind the Impl PIMPL.

// SQLite-backed disk cache for decoded full-resolution images.
// Callers pass fileIdentityKey (path|size|mtimeMs). That string is persisted,
// so an mtime change misses the old row instead of aliasing it.
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

    // Total bytes consumed by all cached blobs (exact payload bytes).
    size_t totalBytes() const;

    // Soft cap on entry count. A successful get() refreshes last access, and
    // eviction drops the least recently used rows. put() checks the cap on a
    // throttle; setMaxEntries() checks immediately. When the cache is over the
    // cap it is pruned to 90% (or to the cap itself when the cap is below 10).
    void setMaxEntries(int n);
    int maxEntries() const;

    // Soft cap on total payload bytes, enforced the same way as the entry cap:
    // least recently used rows are dropped down to 90% of the cap.
    void setMaxBytes(size_t n);
    size_t maxBytes() const;

    // Largest decoded payload (bytes) put() persists. Rows are uncompressed
    // pixel BLOBs, so a full-resolution frame above this costs more SQLite
    // write/read I/O (and lock hold time) than re-decoding it; such frames
    // stay in the memory tiers only. Default kDefaultMaxEntryBytes (16 MiB:
    // e.g. a 4096x3072 RGB24 JPEG, 36 MiB, is not persisted; a 2048x2048
    // RGB24 frame, 12 MiB, is). 0 disables the threshold (hard caps remain).
    static constexpr size_t kDefaultMaxEntryBytes = size_t{16} * 1024 * 1024;
    void setMaxEntryBytes(size_t n)
    {
        m_maxEntryBytes = n;
    }
    size_t maxEntryBytes() const
    {
        return m_maxEntryBytes;
    }
    // Whether put() would persist an image of this payload size.
    bool persistsBytes(size_t bytes) const
    {
        return m_maxEntryBytes == 0 || bytes <= m_maxEntryBytes;
    }
    // put() calls dropped by the entry-size threshold (diagnostics/tests).
    size_t skippedLargeEntries() const
    {
        return m_skippedLarge.load(std::memory_order_relaxed);
    }

    // Clear all cached entries.
    void clear();

    // Remove stale entries for paths no longer present.
    void prune(const std::set<std::string> &validKeys);

    // Enable/disable disk caching (default: enabled)
    void setEnabled(bool on)
    {
        m_enabled = on;
    }
    bool isEnabled() const
    {
        return m_enabled;
    }

  private:
    DiskCache();
    ~DiskCache();
    DiskCache(const DiskCache &) = delete;
    void operator=(const DiskCache &) = delete;

    void ensureTable();
    void openDb();
    void enforceLimits(const QSqlDatabase &db);

    // Returns a QSqlDatabase connection owned by the *current* thread, opened
    // against the same SQLite file as the main-thread template connection.
    // QSqlDatabase is thread-bound: a connection created on one thread must not
    // be touched from another. This gives every TaskScheduler worker thread its
    // own connection so parallel load() calls stop sharing (and corrupting) the
    // main-thread connection. The m_mutex still serializes SQL statements.
    QSqlDatabase connectionForThread() const;

    class Impl; // hides Qt SQL headers from this header
    Impl *m_impl = nullptr;

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
    std::atomic_bool m_enabled{true};
    std::string m_dbPath;
    int m_maxEntries = 100000;
    size_t m_maxBytes = 2147483648ULL; // 2 GB default cap
    std::atomic<size_t> m_maxEntryBytes{kDefaultMaxEntryBytes};
    std::atomic<size_t> m_skippedLarge{0};
    // Inserts since the last limit enforcement (see put()/enforceLimits()).
    int m_writesSinceEnforce = 0;
};
