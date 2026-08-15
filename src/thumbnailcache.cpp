#include "thumbnailcache.h"

#include "runtime_storage.h"

#include "core/trace/Trace.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstddef>

ThumbnailCache &ThumbnailCache::instance()
{
    static ThumbnailCache inst;
    return inst;
}

QString ThumbnailCache::cacheDir() const
{
    const QString base = mviewer::runtime::writableDirectory(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        return QString();
    const QString dir = QDir(base).filePath(QStringLiteral("thumbnails"));
    return QDir().mkpath(dir) ? dir : QString();
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

// Lazily index the existing *.png files once, so files written by an earlier
// process already count against the budget. Recency is seeded from each file's
// mtime (oldest file -> smallest recency), keeping cross-restart LRU order;
// in-session touches use m_clock, which only grows above the seed.
void ThumbnailCache::ensureIndexed()
{
    if (m_indexed)
        return;
    m_indexed = true;
    const QString dir = cacheDir();
    if (dir.isEmpty())
        return;
    QFileInfoList infos = QDir(dir)
                              .entryInfoList(QStringList{QStringLiteral("*.png")},
                                             QDir::Files | QDir::NoDotAndDotDot);
    std::sort(infos.begin(), infos.end(),
              [](const QFileInfo &a, const QFileInfo &b)
              {
                  const QDateTime at = a.lastModified();
                  const QDateTime bt = b.lastModified();
                  if (at != bt)
                      return at < bt;
                  return a.fileName() < b.fileName();
              });
    for (const QFileInfo &fi : infos)
    {
        const QString base = fi.fileName();
        const QString key = base.left(base.size() - 4); // strip ".png"
        insertEntry(key, static_cast<quint64>(fi.size()));
    }
}

void ThumbnailCache::ensureReady()
{
    ensureIndexed();
    pruneToCap();
}

void ThumbnailCache::insertEntry(const QString &key, quint64 fileSize)
{
    dropEntry(key); // replace any prior entry's accounting
    const quint64 rec = ++m_clock;
    m_entries.insert(key, Entry{fileSize, rec});
    m_lru.insert({rec, key});
    m_totalBytes += fileSize;
}

void ThumbnailCache::touchEntry(const QString &key)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    m_lru.erase({it->recency, key});
    it->recency = ++m_clock;
    m_lru.insert({it->recency, key});
}

void ThumbnailCache::dropEntry(const QString &key)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    m_lru.erase({it->recency, key});
    m_totalBytes = m_totalBytes >= it->fileSize ? m_totalBytes - it->fileSize : 0;
    m_entries.erase(it);
}

void ThumbnailCache::removeKey(const QString &key, const QString &file)
{
    // Drop accounting only when the bytes are actually gone from disk. If the
    // removal fails and the file survives, re-account its CURRENT size (it may
    // have been replaced by a differently-sized payload, or be an external
    // file we never indexed) so bytes are never underreported; if the size
    // cannot be determined, keep the existing accounting.
    if (QFile::remove(file) || !QFile::exists(file))
    {
        dropEntry(key);
        return;
    }
    const qint64 sz = QFileInfo(file).size();
    if (sz > 0)
        insertEntry(key, static_cast<quint64>(sz)); // replace/add accounting
}

void ThumbnailCache::pruneToCap()
{
    // Evict least-recently-used entries until usage fits the cap. If a file
    // cannot be deleted (e.g. an OS lock), its bytes stay counted and the
    // entry is rotated to the back of the LRU so the pass can try younger
    // entries and still terminate: a full lap without progress ends the pass.
    const QString dir = cacheDir();
    if (dir.isEmpty())
        return;
    std::size_t blockedInRow = 0;
    while (m_totalBytes > m_maxBytes && !m_lru.empty())
    {
        const QString key = m_lru.begin()->second;
        const QString file = QDir(dir).filePath(key + ".png");
        if (QFile::remove(file) || !QFile::exists(file))
        {
            dropEntry(key);
            blockedInRow = 0;
        }
        else
        {
            // Rotate the undeletable entry to the back via touchEntry so the
            // LRU node and Entry.recency stay one-to-one (a later touch/drop
            // can erase the exact pair); the bytes stay counted.
            touchEntry(key);
            if (++blockedInRow >= m_lru.size())
                return; // every remaining candidate is blocked
        }
    }
}

// Replace the entry atomically: QSaveFile writes a temp file and renames it
// into place on commit(), so a crash never leaves a half-written cache entry
// and an overwrite never surfaces a torn PNG.
bool ThumbnailCache::writeFileAtomically(const QString &file, const QImage &img)
{
    QSaveFile save(file);
    if (!save.open(QIODevice::WriteOnly))
        return false;
    if (!img.save(&save, "PNG"))
        return false;
    return save.commit();
}

bool ThumbnailCache::get(const QString &path, int size, QImage &out)
{
    MV_TRACE_SCOPED("ThumbnailCache::get");
    QMutexLocker lock(&m_mutex);
    ensureReady();
    const QString dir = cacheDir();
    if (dir.isEmpty())
        return false;
    const QString key = keyFor(path, size);
    const QString file = QDir(dir).filePath(key + ".png");
    if (QFile::exists(file))
    {
        // Stat before decoding so an external add/overwrite is accounted and
        // the cap enforced before we commit to loading; if the file is evicted
        // by that enforcement, skip the decode.
        const qint64 sz = QFileInfo(file).size();
        if (sz > 0)
        {
            const auto entryIt = m_entries.find(key);
            if (entryIt == m_entries.end() || entryIt->fileSize != static_cast<quint64>(sz))
            {
                insertEntry(key, static_cast<quint64>(sz)); // replace/add accounting
                pruneToCap(); // the newly-seen bytes may push usage over cap
            }
        }
        if (!QFile::exists(file)) // evicted by the cap enforcement above
            return false;

        QImage img;
        if (img.load(file))
        {
            out = std::move(img);
            touchEntry(key);
            // Best-effort: bump the on-disk mtime so the next process's startup
            // scan orders this entry as recently used. The file must be open
            // for the call to take effect; runs on the caller's (worker) thread.
            QFile f(file);
            if (f.open(QIODevice::ReadWrite))
            {
                (void)f.setFileTime(QDateTime::currentDateTime(),
                                    QFileDevice::FileModificationTime);
                f.close();
            }
            return true;
        }
        // Corrupt payload: remove the file best-effort; accounting follows the
        // disk only if the removal actually happened.
        removeKey(key, file);
        return false;
    }
    if (m_entries.contains(key))
        removeKey(key, file); // file vanished externally; drop stale accounting
    return false;
}

void ThumbnailCache::put(const QString &path, int size, const QImage &img)
{
    MV_TRACE_SCOPED("ThumbnailCache::put");
    if (img.isNull())
        return;
    QMutexLocker lock(&m_mutex);
    ensureReady();
    const QString dir = cacheDir();
    if (dir.isEmpty())
        return;
    const QString key = keyFor(path, size);
    const QString file = QDir(dir).filePath(key + ".png");

    if (m_maxBytes == 0)
    {
        removeKey(key, file); // zero budget: never persist
        return;
    }

    // Persist BEFORE touching accounting: a failed write must leave any prior
    // valid entry and its bytes untouched (QSaveFile never surfaces a torn PNG).
    if (!writeFileAtomically(file, img))
        return;
    const qint64 rawSize = QFileInfo(file).size();
    if (rawSize <= 0)
    {
        removeKey(key, file); // an empty/illegal file cannot be cached
        return;
    }
    const quint64 sz = static_cast<quint64>(rawSize);
    if (sz > m_maxBytes)
    {
        removeKey(key, file); // oversized payload can never fit the budget
        return;
    }
    insertEntry(key, sz); // replaces the prior entry's accounting
    pruneToCap();
}

quint64 ThumbnailCache::maxBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_maxBytes;
}

quint64 ThumbnailCache::totalBytes()
{
    QMutexLocker lock(&m_mutex);
    ensureReady();
    return m_totalBytes;
}

void ThumbnailCache::setMaxBytes(quint64 n)
{
    QMutexLocker lock(&m_mutex);
    ensureIndexed();
    m_maxBytes = n;
    pruneToCap();
}

void ThumbnailCache::clear()
{
    QMutexLocker lock(&m_mutex);
    ensureIndexed();
    const QString dir = cacheDir();
    if (dir.isEmpty())
    {
        m_entries.clear();
        m_lru.clear();
        m_totalBytes = 0;
        m_clock = 0;
        return;
    }
    // Delete every known file; entries the filesystem refuses to delete keep
    // their accounting so totalBytes() stays accurate.
    QHash<QString, Entry> surviving;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
    {
        const QString file = QDir(dir).filePath(it.key() + ".png");
        if (!QFile::remove(file) && QFile::exists(file))
        {
            surviving.insert(it.key(), it.value());
            continue;
        }
    }
    if (surviving.isEmpty())
    {
        m_entries.clear();
        m_lru.clear();
        m_totalBytes = 0;
        m_clock = 0;
        m_indexed = false; // folder is empty; re-scan on next access
    }
    else
    {
        m_entries = std::move(surviving);
        m_lru.clear();
        quint64 bytes = 0;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        {
            m_lru.insert({it->recency, it.key()});
            bytes += it->fileSize;
        }
        m_totalBytes = bytes;
        // Keep m_indexed so surviving entries are not double-counted.
    }
}
