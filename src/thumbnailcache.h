#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

#include <mutex>
#include <set>
#include <thread>
#include <utility>

// M25: on-disk thumbnail cache with a real cache identity.
// M29/P2-05: bounded, thread-safe, approximately-LRU on-disk thumbnail cache.
//
// The key covers source path + source modification identity (mtime, size) +
// REQUESTED THUMBNAIL SIZE + a schema version, so:
//   * a 64px thumbnail can never be served to a 240px request (no upscaled
//     "cache hits" masquerading as higher-resolution thumbnails);
//   * a schema change (new algorithm / payload format) invalidates old files.
//
// Capacity model: the thumbnail folder is kept at or below maxBytes() (default
// 512 MiB). Usage is tracked from the ACTUAL on-disk file sizes in an in-memory
// index. The first get()/put() starts a background bootstrap so the hot path
// only probes the exact identity it needs; totalBytes(), setMaxBytes() and
// clear() join that bootstrap and provide a complete accounting view. Files
// left behind by an earlier process therefore become budget-visible
// asynchronously, while a cache hit never waits for a directory-wide scan.
// put() evicts the least-recently-used entries known at the time; once the
// bootstrap completes the full cap is enforced. A successful get() refreshes
// the session-local approximate LRU. Startup still seeds order from file mtime,
// while cache hits avoid write amplification. A file another process adds
// after the index was built is picked up and counted by get(). The cap is
// best-effort: if a file cannot be deleted (e.g. an OS lock), its bytes stay
// counted and pruning moves on instead of looping.
//
// Thread-safe: the thumbnail worker threads read and write through here
// (QImage payload is safe off the GUI thread; QPixmap is not). All disk I/O
// happens on the caller's thread except the one-time bootstrap scan, which is
// deliberately detached from the GUI-facing thumbnail request.
class ThumbnailCache
{
  public:
    // Bump when the payload format or the key semantics change.
    // M36: cached payloads are display-ready (ICC converted), so invalidate
    // the pre-M36 analysis-domain PNGs.
    static constexpr int kSchemaVersion = 3;

    // Default budget for the on-disk thumbnail folder: 512 MiB.
    static constexpr quint64 kDefaultMaxBytes = 512ULL * 1024ULL * 1024ULL;

    static ThumbnailCache &instance();

    // Returns true and fills `out` when a cache entry exists for `path` at
    // `size` (and the source file identity still matches). A hit refreshes
    // recency and counts the file if it appeared after indexing; a corrupt
    // entry is removed best-effort.
    bool get(const QString &path, int size, QImage &out);

    // Persists `img` (a thumbnail) for `path` at `size`, then evicts
    // least-recently-used entries while total usage exceeds `maxBytes()`.
    void put(const QString &path, int size, const QImage &img);

    // The identity key used for `path` at `size` (public for tests).
    static QString keyFor(const QString &path, int size);

    // Current byte budget for the on-disk thumbnail folder.
    quint64 maxBytes() const;

    // On-disk bytes currently accounted for (actual file sizes, not pixel
    // payloads). Always <= maxBytes() after put()/setMaxBytes(). The call joins
    // the one-time bootstrap if it is still running.
    quint64 totalBytes();

    // Sets the byte budget and prunes immediately so usage falls to <= `n`.
    void setMaxBytes(quint64 n);

    // Deletes every cached thumbnail best-effort and resets usage to zero;
    // bytes the filesystem refuses to remove stay accounted.
    void clear();

  private:
    struct Entry
    {
        quint64 fileSize = 0; // on-disk byte size of the *.png file
        quint64 recency = 0;  // monotonic LRU clock; larger = more recent
    };

    ThumbnailCache() = default;
    ~ThumbnailCache();
    QString cacheDir() const;

    void ensureIndexed();
    void ensureReady();
    void scheduleBootstrap();
    void insertEntry(const QString &key, quint64 fileSize);
    void touchEntry(const QString &key);
    void dropEntry(const QString &key);
    void removeKey(const QString &key, const QString &file);
    void pruneToCap();
    static bool writeFileAtomically(const QString &file, const QImage &img);

    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    std::set<std::pair<quint64, QString>> m_lru; // (recency, key), oldest first
    quint64 m_clock = 0;
    quint64 m_totalBytes = 0;
    quint64 m_maxBytes = kDefaultMaxBytes;
    bool m_indexed = false;
    std::mutex m_bootstrapMutex;
    std::thread m_bootstrapThread;
    bool m_bootstrapScheduled = false;
};
