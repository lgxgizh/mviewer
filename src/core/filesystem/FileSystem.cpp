#include "core/filesystem/FileSystem.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

std::vector<std::string> FileSystem::imageFilters()
{
    QStringList filters{"*.jpg", "*.jpeg", "*.bmp", "*.png"};
    std::vector<std::string> result;
    result.reserve(filters.size());
    for (const QString &f : filters) {
        result.push_back(f.toStdString());
    }
    return result;
}

std::vector<std::string> FileSystem::listImages(const std::string &dir, int max)
{
    QDir d(QString::fromStdString(dir));
    if (!d.exists())
        return {};
    QStringList filters{"*.jpg", "*.jpeg", "*.bmp", "*.png"};
    QFileInfoList entries =
        d.entryInfoList(filters, QDir::Files, QDir::Name);
    std::vector<std::string> result;
    result.reserve(std::min(static_cast<int>(entries.size()), max));
    for (const QFileInfo &fi : entries) {
        result.push_back(fi.absoluteFilePath().toStdString());
        if (result.size() >= static_cast<size_t>(max))
            break;
    }
    return result;
}

bool FileSystem::isImage(const std::string &path)
{
    const QString suffix = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    return suffix == "jpg" || suffix == "jpeg" || suffix == "bmp" ||
           suffix == "png";
}
