#pragma once

// MViewer's app-private trash: the fallback used when the OS recycle bin refuses
// a file (some network shares do). It is bounded - 30 days or 2 GiB, whichever is
// hit first - and pruned off the UI thread.
//
// Extracted from thumbnailpanel_fileops.cpp so the delete/trash flow and its
// storage policy live in one named place.

#include <QObject>
#include <QString>

#include <cstdint>
#include <string>

namespace mviewer::ui
{

class ThumbnailTrash final
{
  public:
    static constexpr qint64 kMaxAgeDays = 30;
    static constexpr qint64 kMaxBytes = qint64(2) * 1024 * 1024 * 1024;

    /// App-private trash directory, or an empty string if the data dir is unknown.
    static QString directory();

    /// Bound the trash on the scheduler's background pool (never the UI thread).
    static void pruneInBackground(const QString &trashDir);

    /// OS recycle bin; `pathInTrash` receives the new path on success.
    static bool moveToSystem(const std::string &source, std::string &pathInTrash);
};

} // namespace mviewer::ui
