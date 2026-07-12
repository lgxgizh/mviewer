#include "FileSystem.h"

#include <QDir>
#include <QFileInfo>

QStringList FileSystem::imageFilters()
{
    return {"*.jpg", "*.jpeg", "*.bmp", "*.png"};
}

QStringList FileSystem::listImages(const QString &dir, int max)
{
    QDir d(dir);
    if (!d.exists())
        return {};
    QFileInfoList entries =
        d.entryInfoList(imageFilters(), QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(entries.size());
    for (const QFileInfo &fi : entries) {
        result.append(fi.absoluteFilePath());
        if (result.size() >= max)
            break;
    }
    return result;
}

bool FileSystem::isImage(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "jpg" || suffix == "jpeg" || suffix == "bmp" ||
           suffix == "png";
}
