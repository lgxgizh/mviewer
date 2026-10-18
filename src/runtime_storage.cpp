#include "runtime_storage.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>

#include <mutex>
#include <unordered_map>

namespace mviewer::runtime
{
namespace
{

QString locationName(QStandardPaths::StandardLocation location)
{
    switch (location)
    {
    case QStandardPaths::AppDataLocation:
        return QStringLiteral("data");
    case QStandardPaths::AppConfigLocation:
        return QStringLiteral("config");
    case QStandardPaths::CacheLocation:
        return QStringLiteral("cache");
    default:
        return QStringLiteral("runtime");
    }
}

bool makeWritable(const QString &path)
{
    if (path.isEmpty() || QDir(path).isRoot())
        return false;
    if (!QDir().mkpath(path))
        return false;
    const QFileInfo info(path);
    if (!info.isDir())
        return false;

    // QFileInfo::isWritable() is not a reliable ACL probe on all Windows
    // filesystems. A short-lived file creation is the contract we actually
    // need, and QTemporaryFile removes the probe on every return path.
    QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".mviewer_probe_XXXXXX")));
    probe.setAutoRemove(true);
    return probe.open();
}

std::mutex &writableDirectoryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int, QString> &writableDirectoryCache()
{
    static std::unordered_map<int, QString> cache;
    return cache;
}

} // namespace

QString writableDirectory(QStandardPaths::StandardLocation location)
{
    const int key = static_cast<int>(location);
    {
        std::lock_guard<std::mutex> lock(writableDirectoryMutex());
        const auto it = writableDirectoryCache().find(key);
        if (it != writableDirectoryCache().end())
            return it->second;
    }

    QString result;
    const QString standard = QStandardPaths::writableLocation(location);
    if (makeWritable(standard))
    {
        result = QDir::cleanPath(standard);
    }
    else
    {
        const QString temp = QDir::tempPath();
        if (!temp.isEmpty() && !QDir(temp).isRoot())
        {
            const QString fallback =
                QDir(temp).filePath(QStringLiteral("MViewer/runtime/") + locationName(location));
            if (makeWritable(fallback))
                result = QDir::cleanPath(fallback);
        }
    }

    std::lock_guard<std::mutex> lock(writableDirectoryMutex());
    const auto [it, inserted] = writableDirectoryCache().emplace(key, result);
    if (!inserted)
        return it->second;
    return result;
}

void resetWritableDirectoryCacheForTests()
{
    std::lock_guard<std::mutex> lock(writableDirectoryMutex());
    writableDirectoryCache().clear();
}

QString filePath(QStandardPaths::StandardLocation location, const QString &fileName)
{
    if (fileName.isEmpty())
        return QString();
    const QString dir = writableDirectory(location);
    return dir.isEmpty() ? QString() : QDir(dir).filePath(fileName);
}

void configureSettings()
{
    const QString configDir = writableDirectory(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty())
        return;

    // Native Windows QSettings targets the registry, which is not a reliable
    // application data store in locked-down deployments or hermetic tests.
    // Keep the QSettings API, but make its backing file part of the same
    // writable AppConfig contract as the JSON state files.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
}

} // namespace mviewer::runtime
