#include "core/filesystem/FileSystem.h"

#include "core/image/ImageFormats.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

std::vector<std::string> FileSystem::imageFilters()
{
    // M25: single source of truth — the decoder registry's format set, in the
    // Qt "*.ext" wildcard convention (RAW/WebP/GIF included).
    return mviewer::core::ImageFormats::wildcardFilters();
}

std::vector<std::string> FileSystem::listImages(const std::string &dir, int max)
{
    QDir d(QString::fromStdString(dir));
    if (!d.exists())
        return {};
    const QStringList filters = [&]()
    {
        QStringList f;
        for (const auto &w : mviewer::core::ImageFormats::wildcardFilters())
            f << QString::fromStdString(w);
        return f;
    }();
    QFileInfoList entries = d.entryInfoList(filters, QDir::Files, QDir::Name);
    std::vector<std::string> result;
    result.reserve(std::min(static_cast<int>(entries.size()), max));
    for (const QFileInfo &fi : entries)
    {
        result.push_back(fi.absoluteFilePath().toStdString());
        // max <= 0 means "no limit" (used by large-corpus scans).
        if (max > 0 && result.size() >= static_cast<size_t>(max))
            break;
    }
    return result;
}

bool FileSystem::isImage(const std::string &path)
{
    return mviewer::core::ImageFormats::isSupportedPath(path);
}
