#include "thumbnailcache.h"

#include "core/trace/Trace.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

ThumbnailCache &ThumbnailCache::instance()
{
    static ThumbnailCache inst;
    return inst;
}

QString ThumbnailCache::cacheDir() const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(dir);
    return dir;
}

QString ThumbnailCache::keyFor(const QString &path, int size)
{
    const QFileInfo fi(path);
    // Identity = path + source mtime + source size + requested thumbnail size
    // + schema version. Any of these changing invalidates the entry.
    const QString raw = path + "|" + QString::number(fi.lastModified().toSecsSinceEpoch()) + "|" +
                        QString::number(fi.size()) + "|" + QString::number(size) + "|v" +
                        QString::number(kSchemaVersion);
    return QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex();
}

bool ThumbnailCache::get(const QString &path, int size, QImage &out)
{
    MV_TRACE_SCOPED("ThumbnailCache::get");
    QMutexLocker lock(&m_mutex);
    const QString file = cacheDir() + "/" + keyFor(path, size) + ".png";
    if (QFile::exists(file))
    {
        QImage img;
        if (img.load(file))
        {
            out = std::move(img);
            return true;
        }
    }
    return false;
}

void ThumbnailCache::put(const QString &path, int size, const QImage &img)
{
    MV_TRACE_SCOPED("ThumbnailCache::put");
    if (img.isNull())
        return;
    QMutexLocker lock(&m_mutex);
    const QString file = cacheDir() + "/" + keyFor(path, size) + ".png";
    img.save(file, "PNG");
}
