#include "core/image/DiskCache.h"

#include "runtime_storage.h"

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>
#include <cstring>
#include <atomic>
#include <memory>

namespace
{

std::atomic_uint64_t g_connectionSerial{0};

struct ThreadConnectionState
{
    QString name;
    bool initialized = false;
};

thread_local ThreadConnectionState g_threadConnection;

} // namespace

class DiskCache::Impl
{
  public:
    // Main-thread connection, used only during construction (ensureTable) and
    // as the template whose database file every per-thread connection opens.
    // QSqlDatabase is bound to the thread that opened it; it must NEVER be used
    // from another thread. All runtime access goes through connectionForThread().
    QSqlDatabase db;
    QThread *ownerThread = nullptr;
    std::set<std::string> workerConnectionNames;
};

DiskCache::DiskCache()
{
    m_impl = new Impl();
    m_impl->ownerThread = QThread::currentThread();
    openDb();
    ensureTable();
}

DiskCache::~DiskCache()
{
    if (m_impl)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const std::string &name : m_impl->workerConnectionNames)
        {
            const QString qname = QString::fromStdString(name);
            if (!QSqlDatabase::contains(qname))
                continue;
            QSqlDatabase db = QSqlDatabase::database(qname, false);
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(qname);
        }
        m_impl->workerConnectionNames.clear();
        m_impl->db.close();
        m_impl->db = QSqlDatabase();
        if (QSqlDatabase::contains(QStringLiteral("mviewer_disk_cache")))
            QSqlDatabase::removeDatabase(QStringLiteral("mviewer_disk_cache"));
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
    const QString cacheDir = mviewer::runtime::writableDirectory(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty())
    {
        qWarning() << "DiskCache: no writable cache directory; disk tier disabled";
        m_enabled = false;
        return;
    }

    const QString dbPath = QDir(cacheDir).filePath(QStringLiteral("mviewer_disk.db"));
    m_dbPath = dbPath.toUtf8().toStdString();
    m_impl->db = QSqlDatabase::addDatabase("QSQLITE", "mviewer_disk_cache");
    m_impl->db.setDatabaseName(dbPath);
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
    // owner-thread comparison must use the captured owner QThread; comparing
    // currentThreadId() with itself would make every worker look like the
    // owner. A monotonic serial, rather than only the OS thread id, prevents a
    // recycled thread id from reusing a process-global Qt connection name.
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_enabled || m_dbPath.empty())
        return QSqlDatabase();
    if (!g_threadConnection.initialized)
    {
        // QSqlDatabase::addDatabase() mutates a process-global connection
        // registry that is NOT thread-safe. TaskScheduler runs decode tasks on
        // a pool of worker threads, and several of them can reach this branch
        // (first DB touch on that thread) simultaneously -> concurrent
        // addDatabase() calls race on Qt's registry and deadlock. Serialize the
        // creation so only one connection is registered at a time.
        static QMutex s_createMutex;
        QMutexLocker createLock(&s_createMutex);
        if (QThread::currentThread() == m_impl->ownerThread)
        {
            g_threadConnection.name = QStringLiteral("mviewer_disk_cache");
        }
        else
        {
            const auto serial = g_connectionSerial.fetch_add(1, std::memory_order_relaxed);
            g_threadConnection.name =
                QStringLiteral("mviewer_disk_cache_worker_%1").arg(serial);
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", g_threadConnection.name);
            db.setDatabaseName(QString::fromUtf8(m_dbPath.data(), static_cast<int>(m_dbPath.size())));
            if (!db.open())
            {
                qWarning() << "DiskCache: worker connection failed:" << db.lastError().text();
                db = QSqlDatabase();
                QSqlDatabase::removeDatabase(g_threadConnection.name);
                g_threadConnection.name.clear();
            }
            else
            {
                m_impl->workerConnectionNames.insert(g_threadConnection.name.toStdString());
            }
        }
        g_threadConnection.initialized = true;
    }
    return g_threadConnection.name.isEmpty()
               ? QSqlDatabase()
               : QSqlDatabase::database(g_threadConnection.name, false);
}

void DiskCache::setMaxEntries(int n)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_maxEntries = n;
    if (m_enabled)
        enforceLimits(connectionForThread());
}

int DiskCache::maxEntries() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_maxEntries;
}

void DiskCache::setMaxBytes(size_t n)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_maxBytes = n;
    if (m_enabled)
        enforceLimits(connectionForThread());
}

size_t DiskCache::maxBytes() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_maxBytes;
}

void DiskCache::enforceLimits(const QSqlDatabase &db)
{
    if (!db.isOpen())
        return;
    while (true)
    {
        QSqlQuery count(db);
        if (!count.exec("SELECT COUNT(*), COALESCE(SUM(LENGTH(data)), 0) FROM blobs") ||
            !count.next())
            return;
        const auto entries = count.value(0).toLongLong();
        const auto bytes = count.value(1).toLongLong();
        if (!((m_maxEntries > 0 && entries > m_maxEntries) ||
              (m_maxBytes > 0 && bytes > static_cast<qint64>(m_maxBytes))))
            return;
        QSqlQuery del(db);
        if (!del.exec("DELETE FROM blobs WHERE key = "
                     "(SELECT key FROM blobs ORDER BY ts ASC, key ASC LIMIT 1)"))
            return;
    }
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
    std::memcpy(out.buffer->data(), blob.constData(), bytesToCopy);
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
    q.addBindValue(QByteArray(reinterpret_cast<const char *>(img.buffer->data()),
                              static_cast<int>(img.byteSize())));
    q.exec();

    enforceLimits(connectionForThread());
}

void DiskCache::remove(const std::string &key)
{
    if (!m_enabled)
        return;
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
    if (!m_enabled)
        return 0;
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
    if (!m_enabled)
        return 0;
    if (!connectionForThread().isOpen())
        return 0;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.exec("SELECT COALESCE(SUM(LENGTH(data)), 0) FROM blobs");
    if (q.next())
        return static_cast<size_t>(q.value(0).toLongLong());
    return 0;
}

void DiskCache::clear()
{
    if (!m_enabled)
        return;
    if (!connectionForThread().isOpen())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QSqlQuery q(connectionForThread());
    q.exec("DELETE FROM blobs");
}

void DiskCache::prune(const std::set<std::string> &validKeys)
{
    if (!m_enabled)
        return;
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
