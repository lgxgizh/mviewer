#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

// Browse-mode Ctrl+C: put the selected image files on the clipboard the way
// File Explorer does, so Ctrl+V in an Explorer folder copies the files. Qt
// turns the file URLs into CF_HDROP on Windows. "Preferred DropEffect" =
// DROPEFFECT_COPY tells Explorer this is a copy, not a cut. text/plain holds
// the native paths so pasting into a text field gives the paths.
// UI-layer header (Qt containers are intentional) so tests can check the
// payload without constructing widgets.

namespace mviewer::browseclip
{

// Windows clipboard format name, spelled as Qt's native-format MIME alias.
inline QString preferredDropEffectMime()
{
    return QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
}

// DROPEFFECT_COPY (1) as a little-endian DWORD; DROPEFFECT_MOVE would be 2.
inline QByteArray dropEffectCopyBytes()
{
    return QByteArray("\x01\x00\x00\x00", 4);
}

// Files to copy: the browse selection (already the cross-pane union in split
// browse), else the current image. Only existing regular files are kept, in
// order and without duplicates; archive members and missing files drop out.
inline QStringList copyTargets(const QStringList &selection, const QString &current)
{
    const QStringList candidates = selection.isEmpty() ? QStringList{current} : selection;
    QStringList out;
    QSet<QString> seen;
    for (const QString &path : candidates)
    {
        if (path.isEmpty())
            continue;
        const QFileInfo info(path);
        if (!info.isFile())
            continue;
        const QString absolute = info.absoluteFilePath();
        if (seen.contains(absolute))
            continue;
        seen.insert(absolute);
        out.append(absolute);
    }
    return out;
}

// Caller owns the result until it is handed to QClipboard::setMimeData.
inline QMimeData *makeFileCopyMimeData(const QStringList &paths)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QStringList nativePaths;
    urls.reserve(paths.size());
    nativePaths.reserve(paths.size());
    for (const QString &path : paths)
    {
        urls.append(QUrl::fromLocalFile(path));
        nativePaths.append(QDir::toNativeSeparators(path));
    }
    mime->setUrls(urls);
    mime->setText(nativePaths.join(QLatin1Char('\n')));
    mime->setData(preferredDropEffectMime(), dropEffectCopyBytes());
    return mime;
}

} // namespace mviewer::browseclip
