#include "core/filesystem/FileSystem.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageFormats.h"

#include <QDir>
#include <QString>
#include <QStringList>
#include <algorithm>

std::vector<std::string> FileSystem::imageFilters()
{
    // M25: single source of truth — the decoder registry's format set, in the
    // Qt "*.ext" wildcard convention (RAW/WebP/GIF included).
    return mviewer::core::ImageFormats::wildcardFilters();
}

std::vector<std::string> FileSystem::listImages(const std::string &dir, int max)
{
    QDir d(QString::fromUtf8(dir.data(), static_cast<int>(dir.size())));
    if (!d.exists())
        return {};
    const auto wildcards = mviewer::core::ImageFormats::wildcardFilters();
    QStringList filters;
    filters.reserve(static_cast<int>(wildcards.size()));
    for (const auto &w : wildcards)
        filters.append(QString::fromUtf8(w.data(), static_cast<int>(w.size())));

    const QStringList entries = d.entryList(filters, QDir::Files, QDir::Name);
    std::vector<std::string> result;
    const size_t limit = (max > 0) ? std::min(static_cast<size_t>(entries.size()),
                                              static_cast<size_t>(max))
                                   : static_cast<size_t>(entries.size());
    result.reserve(limit);
    for (const QString &name : entries)
    {
        result.push_back(d.absoluteFilePath(name).toUtf8().toStdString());
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
