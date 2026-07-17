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

QString ThumbnailCache::keyFor(const QString &path) const
{
    const QFileInfo fi(path);
    const QString raw = path + "|" + QString::number(fi.lastModified().toSecsSinceEpoch()) + "|" +
                        QString::number(fi.size());
    return QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex();
}

bool ThumbnailCache::get(const QString &path, QPixmap &out)
{
    MV_TRACE_SCOPED("ThumbnailCache::get");
    QMutexLocker lock(&m_mutex);
    const QString file = cacheDir() + "/" + keyFor(path) + ".png";
    if (QFile::exists(file))
    {
        QPixmap pm;
        if (pm.load(file))
        {
            out = pm;
            return true;
        }
    }
    return false;
}

void ThumbnailCache::put(const QString &path, const QPixmap &pm)
{
    MV_TRACE_SCOPED("ThumbnailCache::put");
    QMutexLocker lock(&m_mutex);
    const QString file = cacheDir() + "/" + keyFor(path) + ".png";
    pm.save(file, "PNG");
}
