#include "core/FileIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QString>

namespace mviewer::core
{
namespace
{

QString qPathFromUtf8(const std::string &utf8Path)
{
    return QString::fromUtf8(utf8Path.data(), static_cast<int>(utf8Path.size()));
}

std::string decimal(int64_t value)
{
    return QString::number(value).toStdString();
}

} // namespace

FileStamp fileStamp(const std::string &utf8Path)
{
    const QFileInfo info(qPathFromUtf8(utf8Path));
    FileStamp stamp;
    stamp.exists = info.exists();
    stamp.sizeBytes = static_cast<int64_t>(info.size());
    const QDateTime modified = info.lastModified();
    stamp.mtimeMs = static_cast<int64_t>(modified.toMSecsSinceEpoch());
    stamp.mtimeSec = static_cast<int64_t>(modified.toSecsSinceEpoch());
    return stamp;
}

std::string fileIdentityKey(const std::string &utf8Path, int64_t sizeBytes, int64_t mtimeMs)
{
    const QString key = qPathFromUtf8(utf8Path) + QLatin1String("|") + QString::number(sizeBytes) +
                        QLatin1String("|") + QString::number(mtimeMs);
    return key.toStdString();
}

std::string fileIdentityKey(const std::string &utf8Path)
{
    const FileStamp stamp = fileStamp(utf8Path);
    return fileIdentityKey(utf8Path, stamp.sizeBytes, stamp.mtimeMs);
}

std::string fileIdentityCompositeSec(const std::string &path, int64_t sizeBytes, int64_t mtimeSec)
{
    return path + "|" + decimal(sizeBytes) + "|" + decimal(mtimeSec);
}

std::string fileRevisionSec(int64_t mtimeSec, int64_t sizeBytes)
{
    return decimal(mtimeSec) + "|" + decimal(sizeBytes);
}

std::string fileRevisionSec(const std::string &utf8Path)
{
    const FileStamp stamp = fileStamp(utf8Path);
    return fileRevisionSec(stamp.mtimeSec, stamp.sizeBytes);
}

std::string fileRevisionMs(int64_t mtimeMs, int64_t sizeBytes)
{
    return decimal(mtimeMs) + "|" + decimal(sizeBytes);
}

std::string fileRevisionMs(const std::string &utf8Path)
{
    const FileStamp stamp = fileStamp(utf8Path);
    if (!stamp.exists)
        return {};
    return fileRevisionMs(stamp.mtimeMs, stamp.sizeBytes);
}

std::string thumbnailDiskKey(const std::string &utf8Path, int edge, int64_t mtimeMs,
                             int64_t sizeBytes, int schemaVersion)
{
    const QString path = qPathFromUtf8(utf8Path);
    const QByteArray prefix =
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    const std::string hex(prefix.constData(), static_cast<size_t>(prefix.size()));
    return hex + "_" + decimal(mtimeMs) + "_" + decimal(sizeBytes) + "_" + std::to_string(edge) +
           "_v" + std::to_string(schemaVersion);
}

std::string legacyUnseparatedIdentity(const std::string &utf8Path, int64_t sizeBytes,
                                      int64_t mtimeSec)
{
    const QString key =
        qPathFromUtf8(utf8Path) + QString::number(sizeBytes) + QString::number(mtimeSec);
    return key.toStdString();
}

} // namespace mviewer::core
