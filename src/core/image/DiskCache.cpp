#include "core/image/DiskCache.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QVariant>
#include <QByteArray>
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>

#include <memory>
#include <cstring>

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
    if (m_impl) {
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
    if (!m_impl->db.open()) {
        qWarning() << "DiskCache: Failed to open DB:" << m_impl->db.lastError().text();
        m_enabled = false;
    }
}

void DiskCache::ensureTable()
{
    if (!m_enabled || !m_impl->db.isOpen()) return;
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

bool DiskCache::get(const std::string &key, ImageData &out)
{
    if (!m_enabled || !m_impl->db.isOpen()) return false;
    QSqlQuery q(m_impl->db);
    q.prepare("SELECT w, h, fmt, data FROM blobs WHERE key = ?");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    if (!q.exec() || !q.next()) return false;

    const int w = q.value(0).toInt();
    const int h = q.value(1).toInt();
    const int fmt = q.value(2).toInt();
    const QByteArray blob = q.value(3).toByteArray();
    if (w <= 0 || h <= 0 || blob.isEmpty()) return false;

    const PixelFormat pf = static_cast<PixelFormat>(fmt);
    out = makeImageData(w, h, pf);
    const size_t bytesToCopy = std::min(static_cast<size_t>(blob.size()), out.byteSize());
    std::memcpy(out.buffer.get(), blob.constData(), bytesToCopy);
    return true;
}

void DiskCache::put(const std::string &key, const ImageData &img)
{
    if (!m_enabled || !m_impl->db.isOpen() || img.isNull()) return;
    QSqlQuery q(m_impl->db);
    q.prepare("INSERT OR REPLACE INTO blobs(key, w, h, fmt, ts, data) VALUES(?, ?, ?, ?, ?, ?)");
    q.addBindValue(QVariant(QString::fromStdString(key)));
    q.addBindValue(img.width);
    q.addBindValue(img.height);
    q.addBindValue(static_cast<int>(img.format));
    q.addBindValue(QVariant::fromValue<qint64>(QDateTime::currentSecsSinceEpoch()));
    q.addBindValue(QByteArray(reinterpret_cast<const char*>(img.buffer.get()),
                             static_cast<int>(img.byteSize())));
    q.exec();
}

void DiskCache::clear()
{
    if (!m_impl->db.isOpen()) return;
    QSqlQuery q(m_impl->db);
    q.exec("DELETE FROM blobs");
}

void DiskCache::prune(const std::set<std::string> &validKeys)
{
    if (!m_impl->db.isOpen()) return;
    QSqlQuery q(m_impl->db);
    q.exec("SELECT key FROM blobs");
    std::set<std::string> stale;
    while (q.next()) {
        std::string k = q.value(0).toString().toStdString();
        if (validKeys.find(k) == validKeys.end())
            stale.insert(k);
    }
    for (const auto &k : stale) {
        QSqlQuery dq(m_impl->db);
        dq.prepare("DELETE FROM blobs WHERE key = ?");
        dq.addBindValue(QVariant(QString::fromStdString(k)));
        dq.exec();
    }
}
