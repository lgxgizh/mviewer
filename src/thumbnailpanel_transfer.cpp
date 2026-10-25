// Copy and move into another directory, including the other split-browse pane.
#include "splitbrowse_transfer.h"
#include "thumbnailpanel_p.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace
{

QString qutf8(const std::string &text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

mviewer::core::DirectoryEntry directoryEntryForTransfer(const QString &path)
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

bool confirmOtherPaneTransfer(ThumbnailPanel *panel, PaneTransferKind kind, bool fromLeft,
                              int count, const QString &destination)
{
    if (!QSettings().value(fileTransferConfirmKey(), true).toBool())
        return true;
    const QString title = kind == PaneTransferKind::Copy ? ThumbnailPanel::tr("Copy to Other Pane")
                                                         : ThumbnailPanel::tr("Move to Other Pane");
    const QString text = otherPaneTransferConfirmText(kind, fromLeft, count, destination);
    return QMessageBox::question(panel, title, text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

} // namespace

void bindFileTransferObserver(ICommand *command,
                              const mviewer::core::FileSystemAdapter::TransferObserver &observer)
{
    if (!command)
        return;
    if (auto *copy = dynamic_cast<FileCopyCommand *>(command))
        copy->setTransferObserver(observer);
    else if (auto *move = dynamic_cast<FileMoveCommand *>(command))
        move->setTransferObserver(observer);
    else if (auto *del = dynamic_cast<FileDeleteCommand *>(command))
        del->setTransferObserver(observer);
}

void ThumbnailPanel::publishTransferIfAny(const ICommand *command)
{
    if (!command)
        return;
    QStringList created;
    QString destinationDir;
    auto take = [&](const std::vector<std::pair<std::string, std::string>> &pairs)
    {
        for (const auto &pair : pairs)
        {
            const QString dest = qutf8(pair.second);
            if (dest.isEmpty())
                continue;
            created.append(dest);
            destinationDir = QFileInfo(dest).absolutePath();
        }
    };
    if (const auto *copy = dynamic_cast<const FileCopyCommand *>(command))
        take(copy->copied());
    else if (const auto *move = dynamic_cast<const FileMoveCommand *>(command))
        take(move->moved());
    // The host refreshes whichever pane shows destinationDir and selects created.
    if (!created.isEmpty())
        emit filesTransferred(destinationDir, created);
}

void ThumbnailPanel::copyPathsInto(const QStringList &paths, const QString &destinationDirectory)
{
    if (paths.isEmpty() || destinationDirectory.isEmpty() || m_fileOperationBusy)
        return;
    auto cmd = std::make_unique<FileCopyCommand>(toStdPaths(paths),
                                                 destinationDirectory.toUtf8().toStdString());
    startCommandFileOperation(std::move(cmd), paths, tr("Copy"));
}

void ThumbnailPanel::movePathsInto(const QStringList &paths, const QString &destinationDirectory)
{
    if (paths.isEmpty() || destinationDirectory.isEmpty() || m_fileOperationBusy)
        return;
    auto cmd = std::make_unique<FileMoveCommand>(toStdPaths(paths),
                                                 destinationDirectory.toUtf8().toStdString());
    startCommandFileOperation(std::move(cmd), paths, tr("Move"));
}

void ThumbnailPanel::copySelectedToOtherPane()
{
    const QStringList paths = selectedPaths();
    const QString other = property(QStringLiteral("otherPaneDirectory")).toString();
    const bool split = m_selection && m_selection->splitModeEnabled();
    const auto decision = decideOtherPaneTransfer(split, paths.size(), m_currentDir, other);
    if (decision == OtherPaneTransferDecision::SameDirectory)
    {
        emit browseStatusChanged(samePaneDirectoryMessage());
        return;
    }
    if (decision != OtherPaneTransferDecision::Available || m_fileOperationBusy)
        return;
    const bool fromLeft = paneSlot() != SelectionModel::PaneSlot::Secondary;
    if (!confirmOtherPaneTransfer(this, PaneTransferKind::Copy, fromLeft, paths.size(), other))
        return;
    copyPathsInto(paths, other);
}

void ThumbnailPanel::moveSelectedToOtherPane()
{
    const QStringList paths = selectedPaths();
    const QString other = property(QStringLiteral("otherPaneDirectory")).toString();
    const bool split = m_selection && m_selection->splitModeEnabled();
    const auto decision = decideOtherPaneTransfer(split, paths.size(), m_currentDir, other);
    if (decision == OtherPaneTransferDecision::SameDirectory)
    {
        emit browseStatusChanged(samePaneDirectoryMessage());
        return;
    }
    if (decision != OtherPaneTransferDecision::Available || m_fileOperationBusy)
        return;
    const bool fromLeft = paneSlot() != SelectionModel::PaneSlot::Secondary;
    if (!confirmOtherPaneTransfer(this, PaneTransferKind::Move, fromLeft, paths.size(), other))
        return;
    movePathsInto(paths, other);
}

void ThumbnailPanel::showTransferredFiles(const QString &destinationDirectory,
                                          const QStringList &createdPaths)
{
    if (m_currentDir.isEmpty() || createdPaths.isEmpty() ||
        !transferDirectoriesMatch(m_currentDir, destinationDirectory))
        return;

    bool missing = false;
    for (const QString &path : createdPaths)
    {
        if (rowForPath(path) < 0)
        {
            missing = true;
            break;
        }
    }
    if (!missing)
    {
        selectPaths(createdPaths, createdPaths.last());
        return;
    }

    mviewer::core::DirectoryDelta delta;
    delta.path = m_currentDir.toUtf8().toStdString();
    for (const QString &path : createdPaths)
    {
        if (rowForPath(path) < 0)
            delta.added.push_back(directoryEntryForTransfer(path));
    }
    m_transferSelectPending = createdPaths;
    if (!delta.added.empty())
        applyDirectoryDelta(delta);
    if (rowForPath(createdPaths.last()) >= 0)
        selectPaths(createdPaths, createdPaths.last());
}
