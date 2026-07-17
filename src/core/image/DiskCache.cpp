#include "core/image/DiskCache.h"

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>
#include <cstring>
#include <memory>

class DiskCache::Impl
{
  public:
    // Main-thread connection, used only during construction (ensureTable) and
    // as the template whose database file every per-thread connection opens.
    // QSqlDatabase is bound to the thread that opened it; it must NEVER be used
    // from another thread. All runtime access goes through connectionForThread().
    QSqlDatabase db;
};

DiskCache::DiskCache()
{
    m_impl = new Impl();
    openDb();
    ensureTable();
}

DiskCache::~DiskCache()
{
    if (m_impl)
    {
        m_impl->db.close();
        delete m_impl;
    }
}

DiskCache &DiskCache::instance()
{
    static DiskCache inst;
    return inst;
}

void DiskCache::openDb()
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    m_dbPath = (cacheDir + "/mviewer_disk.db").toStdString();
    m_impl->db = QSqlDatabase::addDatabase("QSQLITE", "mviewer_disk_cache");
    m_impl->db.setDatabaseName(cacheDir + "/mviewer_disk.db");
    if (!m_impl->db.open())
    {
        qWarning() << "DiskCache: Failed to open DB:" << m_impl->db.lastError().text();
        m_enabled = false;
    }
}

void DiskCache::ensureTable()
{
    if (!m_enabled || !m_impl->db.isOpen())
        return;
    QSqlQuery q(m_impl->db);
    q.exec("CREATE TABLE IF NOT EXISTS blobs ("
           "key TEXT PRIMARY KEY,"
           "w INT,"
           "h INT,"
           "fmt INT,"
           "ts INT64,"
           "data BLOB)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_ts ON blobs(ts)");
}

QSqlDatabase DiskCache::connectionForThread() const
{
    // One QSqlDatabase per thread, each bound to the same SQLite file. The
    // connection name must be unique process-wide, so derive it from the
    // thread id. The main thread keeps the "mviewer_disk_cache" name created in
    // openDb(); reuse it there to avoid a second handle to the same file.
    const Qt::HANDLE tid = QThread::currentThreadId();
    static thread_local QSqlDatabase tlConn;
    static thread_local bool tlInitialized = false;
    if (!tlInitialized)
    {
        // QSqlDatabase::addDatabase() mutates a process-global connection
        // registry that is NOT thread-safe. TaskScheduler runs decode tasks on
        // a pool of worker threads, and several of them can reach this branch
        // (first DB touch on that thread) simultaneously -> concurrent
        // addDatabase() calls race on Qt's registry and deadlock. Serialize the
        // creation so only one connection is registered at a time.
        static QMutex s_createMutex;
        QMutexLocker createLock(&s_createMutex);
        const QString name =
            (tid == QThread::currentThreadId())
                ? QStringLiteral("mviewer_disk_cache")
                : QString("mviewer_disk_cache_t%1").arg(reinterpret_cast<quintptr>(tid));
        if (name != QStringLiteral("mviewer_disk_cache"))
        {
            tlConn = QSqlDatabase::addDatabase("QSQLITE", name);
            tlConn.setDatabaseName(QString::fromStdString(m_dbPath));
            tlConn.open();
        }
        else
        {
            tlConn = m_impl->db; // main thread: reuse the already-open connection
        }
        tlInitialized = true;
    }
    return tlConn;
}

bool DiskCache::get(const std::string &key, ImageData &out)
{
    if (!m_enabled || !connectionForThread().isOpen())
        return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.prepare("SELECT w, h, fmt, data FROM blobs WHERE key = ?");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    if (!q.exec() || !q.next())
        return false;

    const int w = q.value(0).toInt();
    const int h = q.value(1).toInt();
    const int fmt = q.value(2).toInt();
    const QByteArray blob = q.value(3).toByteArray();
    if (w <= 0 || h <= 0 || blob.isEmpty())
        return false;

    const PixelFormat pf = static_cast<PixelFormat>(fmt);
    out = makeImageData(w, h, pf);
    const size_t bytesToCopy = std::min(static_cast<size_t>(blob.size()), out.byteSize());
    std::memcpy(out.buffer.get(), blob.constData(), bytesToCopy);
    return true;
}

void DiskCache::put(const std::string &key, const ImageData &img)
{
    if (!m_enabled || !connectionForThread().isOpen() || img.isNull())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.prepare("INSERT OR REPLACE INTO blobs(key, w, h, fmt, ts, data) VALUES(?, "
              "?, ?, ?, ?, ?)");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    q.addBindValue(img.width);
    q.addBindValue(img.height);
    q.addBindValue(static_cast<int>(img.format));
    q.addBindValue(QVariant::fromValue<qint64>(QDateTime::currentSecsSinceEpoch()));
    q.addBindValue(QByteArray(reinterpret_cast<const char *>(img.buffer.get()),
                              static_cast<int>(img.byteSize())));
    q.exec();

    // 容量限制：超出任一上限时删除最旧条目，直到两个指标都达标。
    bool pruned = false;
    while ((m_maxEntries > 0 && static_cast<int>(entryCount()) > m_maxEntries) ||
           (m_maxBytes > 0 && totalBytes() > m_maxBytes))
    {
        QSqlQuery dq(connectionForThread());
        dq.prepare("DELETE FROM blobs WHERE key = "
                   "(SELECT key FROM blobs ORDER BY ts ASC LIMIT 1)");
        if (!dq.exec())
            break;
        pruned = true;
    }
    if (pruned)
        qDebug() << "DiskCache: pruned to" << entryCount() << "entries," << totalBytes() << "bytes";
}

void DiskCache::remove(const std::string &key)
{
    if (!connectionForThread().isOpen())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.prepare("DELETE FROM blobs WHERE key = ?");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    q.exec();
}

size_t DiskCache::entryCount() const
{
    if (!connectionForThread().isOpen())
        return 0;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.exec("SELECT COUNT(*) FROM blobs");
    if (q.next())
        return static_cast<size_t>(q.value(0).toLongLong());
    return 0;
}

size_t DiskCache::totalBytes() const
{
    if (!connectionForThread().isOpen())
        return 0;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    // Approximate: width * height * 3 bytes per entry (RGB24)
    q.exec("SELECT COALESCE(SUM(CAST(w AS INTEGER) * CAST(h AS INTEGER) * 3), 0) "
           "FROM blobs");
    if (q.next())
        return static_cast<size_t>(q.value(0).toLongLong());
    return 0;
}

void DiskCache::clear()
{
    if (!connectionForThread().isOpen())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.exec("DELETE FROM blobs");
}

void DiskCache::prune(const std::set<std::string> &validKeys)
{
    if (!connectionForThread().isOpen())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.exec("SELECT key FROM blobs");
    std::set<std::string> stale;
    while (q.next())
    {
        std::string k = q.value(0).toString().toStdString();
        if (validKeys.find(k) == validKeys.end())
            stale.insert(k);
    }
    for (const auto &k : stale)
    {
        QSqlQuery dq(connectionForThread());
        dq.prepare("DELETE FROM blobs WHERE key = ?");
        dq.addBindValue(QVariant(QString::fromStdString(k)));
        dq.exec();
    }
}
