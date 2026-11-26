#include "thumbnailtrash.h"

#include "core/scheduler/TaskScheduler.h"
#include "runtime_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace mviewer::ui
{
namespace
{
// Every permanent removal is logged.
void prune(const QString &trashDir)
{
    QDir dir(trashDir);
    if (trashDir.isEmpty() || !dir.exists())
        return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Hidden, QDir::Time);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-ThumbnailTrash::kMaxAgeDays);
    qint64 total = 0;
    for (const QFileInfo &info : entries) // newest first
    {
        if (info.lastModified() < cutoff || total + info.size() > ThumbnailTrash::kMaxBytes)
        {
            const bool removed = QFile::remove(info.absoluteFilePath());
            qInfo().noquote() << "internal trash prune:"
                              << (removed ? "removed" : "failed to remove")
                              << info.absoluteFilePath() << info.size() << "bytes";
            continue;
        }
        total += info.size();
    }
}
} // namespace

QString ThumbnailTrash::directory()
{
    const QString dataDir =
        mviewer::runtime::writableDirectory(QStandardPaths::GenericDataLocation);
    return dataDir.isEmpty() ? QString() : QDir(dataDir).filePath("trash");
}

void ThumbnailTrash::pruneInBackground(const QString &trashDir)
{
    // TaskScheduler's background pool, not QThreadPool::globalInstance(): the
    // global pool is unbounded and uninstrumented, so a burst of file operations
    // could spawn dozens of threads with nobody watching.
    ::TaskScheduler::instance().submit(::TaskScheduler::Priority::Background,
                                       [trashDir](const ::TaskScheduler::TaskContext &)
                                       { prune(trashDir); });
}

bool ThumbnailTrash::moveToSystem(const std::string &source, std::string &pathInTrash)
{
    QString trashed;
    if (!QFile::moveToTrash(QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size())),
                            &trashed))
        return false;
    pathInTrash = trashed.toUtf8().toStdString();
    return true;
}

} // namespace mviewer::ui
