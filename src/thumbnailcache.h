#pragma once

#include <QMutex>
#include <QPixmap>
#include <QString>

// Simple on-disk thumbnail cache keyed by absolute path + mtime + size.
// All access happens on the main (GUI) thread, so the mutex is just defensive.
class ThumbnailCache
{
  public:
    static ThumbnailCache &instance();

    // Returns true and fills `out` if a cached thumbnail exists and loads.
    bool get(const QString &path, QPixmap &out);

    // Persists `pm` (a thumbnail) for `path`.
    void put(const QString &path, const QPixmap &pm);

  private:
    ThumbnailCache() = default;
    QString cacheDir() const;
    QString keyFor(const QString &path) const;

    QMutex m_mutex;
};
