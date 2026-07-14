#include "core/image/DiskCache.h"

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <cstring>
#include <memory>

class DiskCache::Impl
{
public:
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

DiskCache& DiskCache::instance()
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

bool DiskCache::get(const std::string& key, ImageData& out)
{
    if (!m_enabled || !m_impl->db.isOpen())
        return false;
    QSqlQuery q(m_impl->db);
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

void DiskCache::put(const std::string& key, const ImageData& img)
{
    if (!m_enabled || !m_impl->db.isOpen() || img.isNull())
        return;
    QSqlQuery q(m_impl->db);
    q.prepare("INSERT OR REPLACE INTO blobs(key, w, h, fmt, ts, data) VALUES(?, "
              "?, ?, ?, ?, ?)");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    q.addBindValue(img.width);
    q.addBindValue(img.height);
    q.addBindValue(static_cast<int>(img.format));
    q.addBindValue(QVariant::fromValue<qint64>(QDateTime::currentSecsSinceEpoch()));
    q.addBindValue(QByteArray(
        reinterpret_cast<const char*>(img.buffer.get()), static_cast<int>(img.byteSize())));
    q.exec();

    // 容量限制：超出任一上限时删除最旧条目，直到两个指标都达标。
    bool pruned = false;
    while ((m_maxEntries > 0 && static_cast<int>(entryCount()) > m_maxEntries) ||
           (m_maxBytes > 0 && totalBytes() > m_maxBytes))
    {
        QSqlQuery dq(m_impl->db);
        dq.prepare("DELETE FROM blobs WHERE key = "
                   "(SELECT key FROM blobs ORDER BY ts ASC LIMIT 1)");
        if (!dq.exec())
            break;
        pruned = true;
    }
    if (pruned)
        qDebug() << "DiskCache: pruned to" << entryCount() << "entries," << totalBytes() << "bytes";
}

void DiskCache::remove(const std::string& key)
{
    if (!m_impl->db.isOpen())
        return;
    QSqlQuery q(m_impl->db);
    q.prepare("DELETE FROM blobs WHERE key = ?");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    q.exec();
}

size_t DiskCache::entryCount() const
{
    if (!m_impl->db.isOpen())
        return 0;
    QSqlQuery q(m_impl->db);
    q.exec("SELECT COUNT(*) FROM blobs");
    if (q.next())
        return static_cast<size_t>(q.value(0).toLongLong());
    return 0;
}

size_t DiskCache::totalBytes() const
{
    if (!m_impl->db.isOpen())
        return 0;
    QSqlQuery q(m_impl->db);
    // Approximate: width * height * 3 bytes per entry (RGB24)
    q.exec("SELECT COALESCE(SUM(CAST(w AS INTEGER) * CAST(h AS INTEGER) * 3), 0) "
           "FROM blobs");
    if (q.next())
        return static_cast<size_t>(q.value(0).toLongLong());
    return 0;
}

void DiskCache::clear()
{
    if (!m_impl->db.isOpen())
        return;
    QSqlQuery q(m_impl->db);
    q.exec("DELETE FROM blobs");
}

void DiskCache::prune(const std::set<std::string>& validKeys)
{
    if (!m_impl->db.isOpen())
        return;
    QSqlQuery q(m_impl->db);
    q.exec("SELECT key FROM blobs");
    std::set<std::string> stale;
    while (q.next())
    {
        std::string k = q.value(0).toString().toStdString();
        if (validKeys.find(k) == validKeys.end())
            stale.insert(k);
    }
    for (const auto& k : stale)
    {
        QSqlQuery dq(m_impl->db);
        dq.prepare("DELETE FROM blobs WHERE key = ?");
        dq.addBindValue(QVariant(QString::fromStdString(k)));
        dq.exec();
    }
}
