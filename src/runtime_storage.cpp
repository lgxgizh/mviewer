#include "runtime_storage.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>

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

} // namespace

QString writableDirectory(QStandardPaths::StandardLocation location)
{
    const QString standard = QStandardPaths::writableLocation(location);
    if (makeWritable(standard))
        return QDir::cleanPath(standard);

    const QString temp = QDir::tempPath();
    if (temp.isEmpty() || QDir(temp).isRoot())
        return QString();

    const QString fallback = QDir(temp).filePath(QStringLiteral("MViewer/runtime/") +
                                                  locationName(location));
    if (!makeWritable(fallback))
        return QString();
    return QDir::cleanPath(fallback);
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
