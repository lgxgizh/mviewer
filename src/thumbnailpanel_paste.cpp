// Clipboard paste into the folder the gallery is showing.
#include "thumbnailpanel_p.h"

#include <QDateTime>
#include <QImage>
#include <QUrl>
#include <QVariant>

namespace
{

mviewer::core::DirectoryEntry directoryEntryForPaste(const QString &path)
{
    const QFileInfo info(path);
    mviewer::core::DirectoryEntry entry;
    entry.path = path.toUtf8().toStdString();
    entry.filename = info.fileName().toUtf8().toStdString();
    entry.extension = info.suffix().toLower().toUtf8().toStdString();
    entry.size = info.exists() ? static_cast<uint64_t>(info.size()) : 0;
    entry.modifiedEpochMs = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    return entry;
}

QString uniqueDestinationPath(const QDir &dir, const QString &fileName)
{
    if (!dir.exists(fileName))
        return dir.filePath(fileName);
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    for (int n = 2; n < 10000; ++n)
    {
        const QString candidate = suffix.isEmpty()
                                      ? QStringLiteral("%1-%2").arg(base).arg(n)
                                      : QStringLiteral("%1-%2.%3").arg(base).arg(n).arg(suffix);
        if (!dir.exists(candidate))
            return dir.filePath(candidate);
    }
    return {};
}

bool isLocalImageUrl(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;
    const QString path = url.toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path) || !QFileInfo(path).isFile())
        return false;
    return mviewer::core::ImageFormats::isSupportedPath(path.toUtf8().toStdString());
}

QString copyFailure(const QString &fileName, const QString &reason)
{
    const QString detail = reason.trimmed().isEmpty() ? ThumbnailPanel::tr("copy failed") : reason;
    return ThumbnailPanel::tr("Could not copy %1: %2").arg(fileName, detail);
}

} // namespace

QStringList ThumbnailPanel::pasteClipboardIntoCurrentDirectory(QString *failure)
{
    if (failure)
        failure->clear();
    QStringList written;
    QStringList failures;
    if (m_currentDir.isEmpty())
        return written;
    const QDir destDir(m_currentDir);
    if (!destDir.exists())
        return written;

    const QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard)
        return written;
    const QMimeData *mime = clipboard->mimeData();
    if (!mime)
        return written;

    // Prefer file URLs so Explorer/Finder copies keep original names.
    bool attemptedFileCopy = false;
    if (mime->hasUrls())
    {
        for (const QUrl &url : mime->urls())
        {
            if (!isLocalImageUrl(url))
                continue;
            attemptedFileCopy = true;
            const QString source = QFileInfo(url.toLocalFile()).absoluteFilePath();
            const QString fileName = QFileInfo(source).fileName();
            const QString destPath = uniqueDestinationPath(destDir, fileName);
            // uniqueDestinationPath never returns an existing path, so pasting a
            // file that already lives here creates foo-2.jpg instead of no-op.
            if (destPath.isEmpty())
            {
                failures.append(tr("Could not copy %1: no free destination name.").arg(fileName));
                continue;
            }
            QFile sourceFile(source);
            if (!sourceFile.copy(destPath))
            {
                failures.append(copyFailure(fileName, sourceFile.errorString()));
                continue;
            }
            written.append(destPath);
        }
    }

    // A failed file copy must not be replaced by the clipboard's bitmap preview.
    if (written.isEmpty() && !attemptedFileCopy && mime->hasImage())
    {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull())
        {
            const bool jpeg = mime->hasFormat(QStringLiteral("image/jpeg"));
            const QString ext = jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
            const char *format = jpeg ? "JPEG" : "PNG";
            const QString stamp =
                QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            const QString fileName = QStringLiteral("clipboard-%1.%2").arg(stamp, ext);
            const QString destPath = uniqueDestinationPath(destDir, fileName);
            if (destPath.isEmpty())
            {
                failures.append(
                    tr("Could not save the clipboard image: no free destination name."));
            }
            else if (!image.save(destPath, format))
                failures.append(tr("Could not save the clipboard image as %1.").arg(fileName));
            else
                written.append(destPath);
        }
    }

    if (failure && !failures.isEmpty())
    {
        const QString detail = failures.join(QStringLiteral("\n"));
        *failure = written.isEmpty() ? detail
                                     : tr("Pasted %1 file(s). %2").arg(written.size()).arg(detail);
    }
    if (written.isEmpty())
        return written;

    if (m_liveDirectoryMonitoring)
    {
        emit directoryContentsChanged(m_currentDir);
        selectPath(written.last());
    }
    else
    {
        mviewer::core::DirectoryDelta delta;
        delta.path = m_currentDir.toUtf8().toStdString();
        for (const QString &path : written)
            delta.added.push_back(directoryEntryForPaste(path));
        applyDirectoryDelta(delta);
        selectPaths(written, written.last());
    }
    return written;
}
