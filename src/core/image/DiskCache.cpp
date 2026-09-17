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
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace
{

// How many inserts may pass between two limit enforcements. Enforcing after
// every single put meant one full-table aggregate per decoded image.
constexpr int kEnforceInterval = 32;

std::atomic_uint64_t g_connectionSerial{0};

struct ThreadConnectionState
{
    QString name;
    bool initialized = false;
};

thread_local ThreadConnectionState g_threadConnection;

// Connection-level settings that make concurrent access behave: WAL lets readers
// proceed while one writer commits, and a busy timeout makes a concurrent writer
// wait instead of failing immediately (the old unchecked exec() silently dropped
// such failures).
void applyConnectionPragmas(QSqlDatabase &db)
{
    if (!db.isOpen())
        return;
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
}

// Releases a worker thread's own connection when that thread exits. QThreadPool
// expires idle threads and mints new ones, and each new thread used to create a
// connection that was never released (one file handle plus one SQLite page cache
// per thread, for the lifetime of the process). This destructor runs ON the
// exiting thread, which is the only thread allowed to close its connection.
struct ThreadConnectionGuard
{
    ~ThreadConnectionGuard()
    {
        if (!g_threadConnection.initialized || g_threadConnection.name.isEmpty())
            return;
        if (g_threadConnection.name == QStringLiteral("mviewer_disk_cache"))
            return; // owner-thread connection: closed by ~DiskCache
        {
            QSqlDatabase db = QSqlDatabase::database(g_threadConnection.name, false);
            if (db.isValid())
                db.close();
        }
        QSqlDatabase::removeDatabase(g_threadConnection.name);
        g_threadConnection.initialized = false;
    }
};

// Constructed after g_threadConnection, so it is destroyed before it.
thread_local std::unique_ptr<ThreadConnectionGuard> g_connectionGuard;

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
        // Worker-thread connections are released by ThreadConnectionGuard when
        // their thread exits, so only the owner-thread connection is left here.
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
        return;
    }
    applyConnectionPragmas(m_impl->db);
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
            g_threadConnection.name = QStringLiteral("mviewer_disk_cache_worker_%1").arg(serial);
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", g_threadConnection.name);
            db.setDatabaseName(
                QString::fromUtf8(m_dbPath.data(), static_cast<int>(m_dbPath.size())));
            if (!db.open())
            {
                qWarning() << "DiskCache: worker connection failed:" << db.lastError().text();
                db = QSqlDatabase();
                QSqlDatabase::removeDatabase(g_threadConnection.name);
                g_threadConnection.name.clear();
            }
            else
            {
                applyConnectionPragmas(db);
                // Release this connection when the worker thread exits.
                g_connectionGuard = std::make_unique<ThreadConnectionGuard>();
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
    if (m_maxEntries <= 0 && m_maxBytes <= 0)
        return;
    // Delete in chunks instead of one row per full-table aggregate. The old loop
    // re-ran COUNT(*) + SUM(LENGTH(data)) after every single-row delete, so
    // bringing a populated cache back under its cap cost O(n^2) table scans while
    // holding the global cache mutex (all decode workers serialized behind it).
    for (int pass = 0; pass < 64; ++pass)
    {
        QSqlQuery count(db);
        if (!count.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(LENGTH(data)), 0) "
                                       "FROM blobs")) ||
            !count.next())
            return;
        const auto entries = count.value(0).toLongLong();
        const auto bytes = count.value(1).toLongLong();
        const bool overEntries = m_maxEntries > 0 && entries > m_maxEntries;
        const bool overBytes = m_maxBytes > 0 && bytes > static_cast<qint64>(m_maxBytes);
        if (!overEntries && !overBytes)
            return;
        QSqlQuery del(db);
        if (!del.exec(QStringLiteral("DELETE FROM blobs WHERE key IN "
                                     "(SELECT key FROM blobs ORDER BY ts ASC, key ASC LIMIT 512)")))
            return;
        if (del.numRowsAffected() <= 0)
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
    // Reject implausible dimensions before allocating: a corrupt row must not be
    // able to turn a cache read into a huge allocation attempt.
    constexpr int kMaxCachedEdge = 200000;
    if (w > kMaxCachedEdge || h > kMaxCachedEdge)
        return false;

    // Hardened bound: protect against pathological allocations from corrupt DB rows.
    // 256M pixels (~1GB at 32bpp) is the maximum single-image allocation envelope.
    constexpr int64_t kMaxCachedPixels = 256 * 1024 * 1024;
    if (static_cast<int64_t>(w) * h > kMaxCachedPixels)
        return false;

    const PixelFormat pf = static_cast<PixelFormat>(fmt);
    // The format is persisted across versions, so it has to be a known value.
    if (pf != PixelFormat::RGB24 && pf != PixelFormat::RGBA32 && pf != PixelFormat::BGR24 &&
        pf != PixelFormat::BGRA32 && pf != PixelFormat::Grayscale8)
        return false;

    out = makeImageData(w, h, pf);
    if (out.isNull())
        return false;
    // Exact payload size: a truncated/rolled-back write must be a MISS, not a
    // partially-filled image served as valid (the old min() copy zero-padded it
    // and returned true).
    if (static_cast<size_t>(blob.size()) != out.byteSize())
    {
        out = ImageData();
        return false;
    }
    std::memcpy(out.buffer->data(), blob.constData(), out.byteSize());
    return true;
}

void DiskCache::put(const std::string &key, const ImageData &img)
{
    if (!m_enabled || !connectionForThread().isOpen() || img.isNull() || key.empty())
        return;
    if (img.width <= 0 || img.height <= 0)
        return;
    constexpr int64_t kMaxCachedPixels = 256 * 1024 * 1024;
    if (static_cast<int64_t>(img.width) * img.height > kMaxCachedPixels)
        return;
    // A QByteArray length is an int; refusing oversized payloads beats silently
    // truncating them into an unreadable row.
    if (img.byteSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
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
    if (!q.exec())
        qWarning() << "DiskCache: put failed:" << q.lastError().text();

    // Enforcing on every single insert meant a full-table aggregate per decoded
    // image at steady state; a periodic check keeps the cap honoured without
    // paying that on the hot path.
    if (++m_writesSinceEnforce >= kEnforceInterval)
    {
        m_writesSinceEnforce = 0;
        enforceLimits(connectionForThread());
    }
}

void DiskCache::remove(const std::string &key)
{
    if (!m_enabled || key.empty())
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
