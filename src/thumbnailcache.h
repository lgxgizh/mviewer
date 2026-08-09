#pragma once

#include <QImage>
#include <QMutex>
#include <QString>

// M25: on-disk thumbnail cache with a real cache identity.
//
// The key covers source path + source modification identity (mtime, size) +
// REQUESTED THUMBNAIL SIZE + a schema version, so:
//   * a 64px thumbnail can never be served to a 240px request (no upscaled
//     "cache hits" masquerading as higher-resolution thumbnails);
//   * a schema change (new algorithm / payload format) invalidates old files.
//
// Thread-safe: the worker threads read and write through here (QImage payload
// is safe off the GUI thread; QPixmap is not).
class ThumbnailCache
{
  public:
    // Bump when the payload format or the key semantics change.
    static constexpr int kSchemaVersion = 2;

    static ThumbnailCache &instance();

    // Returns true and fills `out` when a cache entry exists for `path` at
    // `size` (and the source file identity still matches).
    bool get(const QString &path, int size, QImage &out);

    // Persists `img` (a thumbnail) for `path` at `size`.
    void put(const QString &path, int size, const QImage &img);

    // The identity key used for `path` at `size` (public for tests).
    static QString keyFor(const QString &path, int size);

  private:
    ThumbnailCache() = default;
    QString cacheDir() const;

    QMutex m_mutex;
};
