// ThumbnailPanel file operations: rename, trash, copy/move, batch export, context menu (M20 P0#3).
#include "thumbnailpanel_p.h"

#include "fileop_targets.h"
#include "fileopmessages.h"
#include "runtime_storage.h"
#include "splitbrowse_delete.h"
#include "splitbrowse_transfer.h"
#include "thumbnailpanel_transfer.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QSettings>
#include <QUrl>
#include <QVariant>

namespace
{
mviewer::core::DirectoryEntry directoryEntryForPath(const QString &path)
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

struct AsyncCommandState
{
    explicit AsyncCommandState(std::unique_ptr<ICommand> value) : command(std::move(value))
    {
    }

    std::unique_ptr<ICommand> command;
    bool succeeded = false;
    bool cancelled = false;
};

QString askTransferDirectory(QWidget *parent, const QString &title, bool splitActive,
                             const QString &otherPaneDir, const QString &currentDir)
{
    const QString last = QSettings().value(lastTransferDirKey()).toString();
    const QString dir = QFileDialog::getExistingDirectory(
        parent, title, defaultTransferDir(splitActive, otherPaneDir, last, currentDir));
    if (!dir.isEmpty())
        QSettings().setValue(lastTransferDirKey(), QDir::cleanPath(dir));
    return dir;
}

struct AsyncCopyState
{
    int copied = 0;
    bool cancelled = false;
    QStringList failures;
};

// Same app-private trash directory as ThumbnailPanel::moveToTrashSelected.
QString mviewerTrashDirectory()
{
    const QString dataDir =
        mviewer::runtime::writableDirectory(QStandardPaths::GenericDataLocation);
    return dataDir.isEmpty() ? QString() : QDir(dataDir).filePath("trash");
}

// Fallback trash (used only when the system recycle bin refuses a file,
// e.g. some network shares): keep it bounded instead of growing forever.
constexpr qint64 kInternalTrashMaxAgeDays = 30;
constexpr qint64 kInternalTrashMaxBytes = qint64(2) * 1024 * 1024 * 1024;

void pruneInternalTrash(const QString &trashDir)
{
    QDir dir(trashDir);
    if (trashDir.isEmpty() || !dir.exists())
        return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Hidden, QDir::Time);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kInternalTrashMaxAgeDays);
    qint64 total = 0;
    for (const QFileInfo &info : entries) // newest first
    {
        if (info.lastModified() < cutoff || total + info.size() > kInternalTrashMaxBytes)
        {
            QFile::remove(info.absoluteFilePath());
            continue;
        }
        total += info.size();
    }
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
bool moveToSystemTrash(const std::string &source, std::string &pathInTrash)
{
    QString trashed;
    if (!QFile::moveToTrash(QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size())),
                            &trashed))
        return false;
    pathInTrash = trashed.toUtf8().toStdString();
    return true;
}

int transferPercent(uintmax_t copied, uintmax_t total)
{
    if (total == 0)
        return copied == 0 ? 0 : 100;
    const auto ratio = static_cast<double>(copied) / static_cast<double>(total);
    return std::clamp(static_cast<int>(ratio * 100.0), 0, 100);
}
} // namespace

uint64_t ThumbnailPanel::beginFileOperationProgress(const QString &label)
{
    m_fileOperationBusy = true;
    const uint64_t generation = ++m_fileOperationGeneration;
    m_fileProgress = new QProgressDialog(label, tr("Cancel"), 0, 100, this);
    m_fileProgress->setWindowModality(Qt::WindowModal);
    m_fileProgress->setAutoClose(false);
    m_fileProgress->setAutoReset(false);
    m_fileProgress->setMinimumDuration(0);
    m_fileProgress->setValue(0);
    connect(m_fileProgress, &QProgressDialog::canceled, this,
            [this, generation]()
            {
                if (generation == m_fileOperationGeneration && m_fileOperationTask)
                    TaskScheduler::cancel(m_fileOperationTask);
            });
    m_fileProgress->show();
    return generation;
}

std::function<void(int)> ThumbnailPanel::makeFileOperationProgressHandler(uint64_t generation)
{
    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> guard(this);
    auto lastProgress = std::make_shared<std::atomic<int>>(-1);
    return [guard, alive, generation, lastProgress](int value)
    {
        if (!alive->load(std::memory_order_relaxed) || !guard)
            return;
        if (lastProgress->exchange(value, std::memory_order_relaxed) == value)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [guard, alive, generation, value]()
            {
                if (!alive->load(std::memory_order_relaxed) || !guard ||
                    generation != guard->m_fileOperationGeneration)
                    return;
                if (guard->m_fileProgress)
                    guard->m_fileProgress->setValue(value);
            },
            Qt::QueuedConnection);
    };
}

void ThumbnailPanel::closeFileOperationProgress()
{
    if (!m_fileProgress)
        return;
    m_fileProgress->close();
    m_fileProgress->deleteLater();
    m_fileProgress = nullptr;
}

void ThumbnailPanel::failFileOperationQueue(const QString &title, const QString &message)
{
    m_fileOperationBusy = false;
    closeFileOperationProgress();
    QMessageBox::warning(this, title, message);
}

void ThumbnailPanel::startCommandFileOperation(std::unique_ptr<ICommand> command,
                                               const QStringList &paths, const QString &label)
{
    if (!command || paths.isEmpty() || m_fileOperationBusy)
        return;

    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> guard(this);
    auto state = std::make_shared<AsyncCommandState>(std::move(command));
    const uint64_t generation = beginFileOperationProgress(label + QStringLiteral("..."));
    const auto onProgress = makeFileOperationProgressHandler(generation);

    m_fileOperationTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::UI,
        [state](const TaskScheduler::TaskContext &ctx)
        {
            const auto observer = [&ctx](uintmax_t copied, uintmax_t total)
            {
                if (ctx.isCancelled())
                    return false;
                ctx.reportProgress(transferPercent(copied, total));
                return !ctx.isCancelled();
            };
            bindFileTransferObserver(state->command.get(), observer);

            try
            {
                state->command->execute();
            }
            catch (...)
            {
                bindFileTransferObserver(state->command.get(), {});
                throw;
            }
            // The command is retained for Undo/Redo. Do not retain the worker
            // callback, whose TaskContext is only valid for this execution.
            bindFileTransferObserver(state->command.get(), {});
            state->succeeded = state->command->lastError().empty();
            state->cancelled = ctx.isCancelled();
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, generation, state, paths, label]() mutable
        {
            QStringList removed;
            for (const QString &path : paths)
            {
                if (!QFileInfo::exists(path))
                    removed.append(path);
            }

            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, generation, state, removed = std::move(removed), label]() mutable
                {
                    if (!alive->load(std::memory_order_relaxed) || !guard ||
                        generation != guard->m_fileOperationGeneration)
                        return;

                    ThumbnailPanel *panel = guard.data();
                    panel->m_fileOperationBusy = false;
                    panel->m_fileOperationTask.reset();
                    if (panel->m_fileProgress)
                    {
                        panel->m_fileProgress->close();
                        panel->m_fileProgress->deleteLater();
                        panel->m_fileProgress = nullptr;
                    }

                    const std::string error =
                        state->command ? state->command->lastError()
                                       : ThumbnailPanel::tr("Command was lost.").toStdString();
                    const bool unresolved = state->command && state->command->hasUnresolvedState();
                    ThumbnailPanelTransfer::publishTransferIfAny(*panel, state->command.get());
                    if (panel->m_cmdStack)
                        panel->m_cmdStack->recordExecuted(std::move(state->command));

                    if (!state->succeeded)
                    {
                        const QString detail = mviewer::ui::fileOpErrorText(error);
                        QMessageBox::warning(
                            panel, label,
                            state->cancelled ? label + ThumbnailPanel::tr("Cancelled.\n") + detail
                                             : label + ThumbnailPanel::tr("Failed.\n") + detail);
                    }

                    if (!panel->m_currentDir.isEmpty())
                    {
                        if (panel->m_liveDirectoryMonitoring)
                            emit panel->directoryContentsChanged(panel->m_currentDir);
                        else
                            panel->refresh();
                    }

                    if (!removed.isEmpty())
                        emit panel->pathsRemoved(removed);

                    // Keep this local so the result is explicit in a debugger
                    // and the unresolved command is still owned by the stack.
                    (void)unresolved;
                },
                Qt::QueuedConnection);
        },
        onProgress);

    if (!m_fileOperationTask)
        failFileOperationQueue(label,
                               tr("Cannot queue %1: background task queue is full.").arg(label));
}

void ThumbnailPanel::startCopyFileOperation(const QStringList &paths,
                                            const QString &destinationDirectory)
{
    if (paths.isEmpty() || destinationDirectory.isEmpty() || m_fileOperationBusy)
        return;

    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> guard(this);
    auto state = std::make_shared<AsyncCopyState>();
    const auto fileSystem = mviewer::core::defaultFileSystemAdapter();
    const uint64_t generation = beginFileOperationProgress(tr("Copying..."));
    const auto onProgress = makeFileOperationProgressHandler(generation);

    m_fileOperationTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::UI,
        [state, paths, destinationDirectory, fileSystem](const TaskScheduler::TaskContext &ctx)
        {
            const auto destinationDir =
                mviewer::core::pathFromUtf8(destinationDirectory.toUtf8().toStdString());
            for (int index = 0; index < paths.size(); ++index)
            {
                if (ctx.isCancelled())
                {
                    state->cancelled = true;
                    break;
                }

                const QString &path = paths.at(index);
                const auto source = mviewer::core::pathFromUtf8(path.toUtf8().toStdString());
                std::string destinationError;
                const auto destination = mviewer::core::collisionFreeDestination(
                    source, destinationDir, fileSystem, destinationError);
                if (destination.empty())
                {
                    state->failures.append(path + ": " +
                                           mviewer::ui::fileOpErrorText(destinationError));
                    continue;
                }

                const int base = (index * 100) / paths.size();
                const int span = ((index + 1) * 100) / paths.size() - base;
                const auto result = mviewer::core::copyFileAtomically(
                    source, destination, fileSystem,
                    [&ctx, base, span](uintmax_t copied, uintmax_t total)
                    {
                        if (ctx.isCancelled())
                            return false;
                        const int local = transferPercent(copied, total);
                        ctx.reportProgress(base + (local * span) / 100);
                        return !ctx.isCancelled();
                    });
                if (result.state == mviewer::core::FileTransferState::Succeeded)
                    ++state->copied;
                else
                {
                    state->failures.append(path + ": " +
                                           mviewer::ui::fileOpErrorText(result.error));
                    if (ctx.isCancelled())
                    {
                        state->cancelled = true;
                        break;
                    }
                }
            }
            if (!ctx.isCancelled() && state->failures.isEmpty())
                ctx.reportProgress(100);
            else if (ctx.isCancelled())
                state->cancelled = true;
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, generation, state]()
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, generation, state]()
                {
                    if (!alive->load(std::memory_order_relaxed) || !guard ||
                        generation != guard->m_fileOperationGeneration)
                        return;
                    ThumbnailPanel *panel = guard.data();
                    panel->m_fileOperationBusy = false;
                    panel->m_fileOperationTask.reset();
                    if (panel->m_fileProgress)
                    {
                        panel->m_fileProgress->close();
                        panel->m_fileProgress->deleteLater();
                        panel->m_fileProgress = nullptr;
                    }
                    const QString summary =
                        ThumbnailPanel::tr("Copy complete: %1 succeeded, %2 failed.%3")
                            .arg(state->copied)
                            .arg(state->failures.size())
                            .arg(state->failures.isEmpty()
                                     ? QString()
                                     : QStringLiteral("\n") + state->failures.join("\n"));
                    if (state->cancelled)
                        QMessageBox::warning(panel, ThumbnailPanel::tr("Copy"),
                                             ThumbnailPanel::tr("Copy cancelled.\n") + summary);
                    else if (state->failures.isEmpty())
                        emit panel->browseStatusChanged(
                            ThumbnailPanel::tr("Successfully copied %1 files").arg(state->copied));
                    else
                        QMessageBox::warning(panel, ThumbnailPanel::tr("Copy"), summary);
                },
                Qt::QueuedConnection);
        },
        onProgress);

    if (!m_fileOperationTask)
        failFileOperationQueue(tr("Copy"), tr("Cannot queue copy: background task queue is full."));
}

void ThumbnailPanel::renameSelected()
{
    QList<int> rows;
    if (selectionModel())
    {
        const QModelIndexList selected = selectionModel()->selectedIndexes();
        for (const QModelIndex &index : selected)
        {
            if (index.column() != 0 || rows.contains(index.row()))
                continue;
            rows.append(index.row());
        }
    }
    const int current = currentIndex().isValid() ? currentIndex().row() : -1;
    const int row = renameTargetRow(rows, current);
    if (row < 0 || row >= m_paths.size())
        return;
    renamePath(m_paths.at(row), rows.size() > 1);
}

void ThumbnailPanel::renamePath(const QString &oldPath, bool multiSelectionNotice)
{
    if (oldPath.isEmpty())
        return;
    const QFileInfo fi(oldPath);
    QInputDialog dialog(this);
    dialog.setWindowTitle(multiSelectionNotice ? tr("Rename %1").arg(fi.fileName()) : tr("Rename"));
    dialog.setLabelText(tr("New file name:"));
    dialog.setTextValue(fi.fileName());
    if (auto *lineEdit = dialog.findChild<QLineEdit *>())
    {
        const QString base = fi.completeBaseName();
        lineEdit->setSelection(0, static_cast<int>(base.length()));
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString newName = dialog.textValue();
    if (newName.isEmpty() || newName == fi.fileName())
        return;
    const QString blocked = renameBlockedReason(fi.absolutePath(), fi.fileName(), newName);
    if (!blocked.isEmpty())
    {
        QMessageBox::warning(this, tr("Rename failed"), blocked);
        return;
    }
    const QString newSuffix = QFileInfo(newName).suffix();
    if (fi.suffix().compare(newSuffix, Qt::CaseInsensitive) != 0 &&
        QMessageBox::question(
            this, tr("Rename"),
            tr("Changing the extension may prevent the file from opening. Continue anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const QString newPath = QDir(fi.absolutePath()).filePath(newName);

    // A-10: reversible rename via CommandStack when available.
    if (m_cmdStack)
    {
        auto cmd = std::make_unique<FileRenameCommand>(oldPath.toUtf8().toStdString(),
                                                       newPath.toUtf8().toStdString());
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, tr("Rename failed"),
                                 mviewer::ui::fileOpErrorText(m_cmdStack->lastError()));
            return;
        }
    }
    else
    {
        auto cmd = std::make_unique<FileRenameCommand>(oldPath.toUtf8().toStdString(),
                                                       newPath.toUtf8().toStdString());
        cmd->execute();
        if (!cmd->lastError().empty())
        {
            QMessageBox::warning(this, tr("Rename failed"),
                                 mviewer::ui::fileOpErrorText(cmd->lastError()));
            return;
        }
    }
    if (!m_currentDir.isEmpty())
    {
        if (m_liveDirectoryMonitoring)
        {
            // The active-directory monitor owns the reconciliation boundary.
            // Park the desired selection until its rename delta reaches model.
            emit directoryContentsChanged(m_currentDir);
        }
        else
        {
            // Headless panel hosts do not install MainWindow's monitor. Apply
            // the known identity migration locally so file operations retain
            // the same row-local behavior without a full model reset.
            mviewer::core::DirectoryDelta delta;
            delta.path = m_currentDir.toUtf8().toStdString();
            delta.renamed.push_back(
                {directoryEntryForPath(oldPath), directoryEntryForPath(newPath)});
            applyDirectoryDelta(delta);
        }
        // M24 (A#8): keep the renamed file selected — Explorer/FastStone
        // parity. The live delta migrates the identity without a model reset.
        selectPath(newPath);
    }
    if (multiSelectionNotice)
    {
        emit browseStatusChanged(
            tr("Renamed the focused file only; use Tools → Batch Processing to rename "
               "multiple files"));
    }
}

void ThumbnailPanel::moveToTrashSelected()
{
    const bool splitActive = m_selection && m_selection->isSplitModeEnabled();
    const QStringList paths =
        splitActive
            ? splitDeleteUnion(m_selection->paneSelection(SelectionModel::PaneSlot::Primary),
                               m_selection->paneSelection(SelectionModel::PaneSlot::Secondary))
            : selectedPaths();
    QString confirm;
    if (splitActive && !paths.isEmpty())
    {
        const QString name = paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QString();
        confirm = splitDeleteConfirmMessage(
            splitDeletePrompt(m_selection->paneSelection(SelectionModel::PaneSlot::Primary),
                              m_selection->paneSelection(SelectionModel::PaneSlot::Secondary)),
            true, name);
    }
    moveToTrashPaths(paths, confirm);
}

void ThumbnailPanel::moveToTrashPaths(const QStringList &paths, const QString &confirmMessage)
{
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    if (QSettings().value(QStringLiteral("confirmDelete"), true).toBool())
    {
        QString prompt = confirmMessage;
        if (prompt.isEmpty())
        {
            const QString name =
                paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QString();
            prompt = splitDeleteConfirmMessage(
                SplitDeletePrompt{0, 0, static_cast<int>(paths.size())}, false, name);
        }
        const auto answer =
            QMessageBox::question(this, tr("Delete confirmation"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    // System recycle bin first (Undo moves the file back from there); the
    // app-private directory is only the fallback, pruned by age and size.
    const QString trashDir = mviewerTrashDirectory();
    if (trashDir.isEmpty() || !QDir().mkpath(trashDir))
    {
        QMessageBox::warning(this, tr("Delete failed"),
                             tr("Unable to create the recycle directory."));
        return;
    }

    pruneInternalTrash(trashDir);
    auto cmd =
        std::make_unique<FileDeleteCommand>(toStdPaths(paths), trashDir.toUtf8().toStdString());
    cmd->setSystemTrash(&moveToSystemTrash);
    startCommandFileOperation(std::move(cmd), paths, tr("Delete"));
}

void ThumbnailPanel::openTrashFolder()
{
    // Deletes prefer the OS recycle bin; open that view first. The app-private
    // directory is only a fallback when moveToTrash fails (e.g. some shares).
#ifdef Q_OS_WIN
    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("shell:RecycleBinFolder")}))
        return;
#elif defined(Q_OS_MACOS)
    if (QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("trash:///")}))
        return;
#else
    if (QDesktopServices::openUrl(QUrl(QStringLiteral("trash:///"))))
        return;
#endif
    const QString trashDir = mviewerTrashDirectory();
    if (trashDir.isEmpty())
    {
        QMessageBox::warning(this, tr("Recycle Bin"), tr("Cannot open the system Recycle Bin."));
        return;
    }
    QDir().mkpath(trashDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(trashDir));
}

void ThumbnailPanel::copySelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    const bool split = m_selection && m_selection->isSplitModeEnabled();
    const QString other = property("otherPaneDirectory").toString();
    const QString dir = askTransferDirectory(this, tr("Copy To..."), split, other, m_currentDir);
    if (dir.isEmpty())
        return;
    startCopyFileOperation(paths, dir);
}

void ThumbnailPanel::moveSelectedTo()
{
    movePathsTo(selectedPaths());
}

void ThumbnailPanel::movePathsTo(const QStringList &paths)
{
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    const bool split = m_selection && m_selection->isSplitModeEnabled();
    const QString other = property("otherPaneDirectory").toString();
    const QString dir = askTransferDirectory(this, tr("Move To..."), split, other, m_currentDir);
    if (dir.isEmpty())
        return;

    ThumbnailPanelTransfer::movePathsInto(*this, paths, dir);
}

void ThumbnailPanel::revealSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString p = QDir::toNativeSeparators(paths.first());
    // startDetached, not execute(): QProcess::execute blocks until the child
    // exits, and handing off to the shell can take hundreds of ms to seconds
    // (busy Explorer / slow or network path) with the UI frozen for all of it.
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", QStringList{QStringLiteral("/select,") + p});
#else
    QProcess::startDetached("xdg-open", QStringList() << QFileInfo(paths.first()).absolutePath());
#endif
}

void ThumbnailPanel::openCurrentFolder()
{
    if (m_currentDir.isEmpty())
        return;
    const QFileInfo info(m_currentDir);
    if (!info.exists() || !info.isDir())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

QStringList ThumbnailPanel::pasteClipboardIntoCurrentDirectory()
{
    QStringList written;
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
    if (mime->hasUrls())
    {
        for (const QUrl &url : mime->urls())
        {
            if (!isLocalImageUrl(url))
                continue;
            const QString source = QFileInfo(url.toLocalFile()).absoluteFilePath();
            const QString destPath = uniqueDestinationPath(destDir, QFileInfo(source).fileName());
            // uniqueDestinationPath never returns an existing path, so pasting a
            // file that already lives here creates foo-2.jpg instead of no-op.
            if (destPath.isEmpty() || !QFile::copy(source, destPath))
                continue;
            written.append(destPath);
        }
    }

    if (written.isEmpty() && mime->hasImage())
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
            if (!destPath.isEmpty() && image.save(destPath, format))
                written.append(destPath);
        }
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
            delta.added.push_back(directoryEntryForPath(path));
        applyDirectoryDelta(delta);
        selectPaths(written, written.last());
    }
    return written;
}

void ThumbnailPanel::batchAnalyzeExport()
{
    // Prefer multi-selection; fall back to the currently visible (filtered) set
    // so rating/flag filters flow into batch analysis export (M17).
    QStringList paths = selectedPaths();
    if (paths.isEmpty())
        paths = visiblePaths();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("Please select an image first, or open a filtered directory."));
        return;
    }

    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    const std::vector<std::string> ids = reg.availableAnalyzers();
    if (ids.empty())
    {
        QMessageBox::warning(this, tr("Batch Analysis Export"),
                             tr("No analyzer is currently available."));
        return;
    }

    // Let the user pick which analyzer to run (default: first registered).
    QStringList labels;
    for (const auto &id : ids)
    {
        const auto info = reg.infoFor(id);
        labels << (info ? QString::fromStdString(info->name) : QString::fromStdString(id));
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Batch Analysis Export"),
                                                 tr("Select an analyzer:"), labels, 0, false, &ok);
    if (!ok || chosen.isEmpty())
        return;
    const int chosenIdx = labels.indexOf(chosen);
    if (chosenIdx < 0 || chosenIdx >= static_cast<int>(ids.size()))
        return;
    const std::string analyzerId = ids[static_cast<size_t>(chosenIdx)];

    const QString out = QFileDialog::getSaveFileName(this, tr("Export analysis results"), QString(),
                                                     tr("CSV (*.csv);;JSON (*.json)"));
    if (out.isEmpty())
        return;

    runBatchAnalyzeExportAsync(paths, analyzerId, out);
    return;
}

void ThumbnailPanel::ensureBatchProgressDialog()
{
    if (m_batchProgress)
        return;
    m_batchProgress =
        new QProgressDialog(tr("Running batch analysis..."), tr("Cancel"), 0, 100, this);
    m_batchProgress->setWindowModality(Qt::WindowModal);
    m_batchProgress->setAutoClose(false);
    m_batchProgress->setMinimumDuration(0);
    connect(m_batchProgress, &QProgressDialog::canceled, this,
            [this]()
            {
                if (m_batchTask)
                    TaskScheduler::cancel(m_batchTask);
            });
}

void ThumbnailPanel::finishBatchAnalyzeExport(bool cancelled, bool writeOk, size_t resultCount,
                                              const QString &output)
{
    m_batchTask.reset();
    if (m_batchProgress)
        m_batchProgress->close();
    if (cancelled)
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("Batch analysis cancelled."));
    }
    else if (!writeOk)
    {
        QMessageBox::critical(this, tr("Batch Analysis Export"),
                              tr("Cannot write: %1").arg(output));
    }
    else
    {
        QMessageBox::information(
            this, tr("Batch Analysis Export"),
            tr("Exported %1 results → %2").arg(static_cast<qlonglong>(resultCount)).arg(output));
    }
}

void ThumbnailPanel::runBatchAnalyzeExportAsync(const QStringList &paths,
                                                const std::string &analyzerId,
                                                const QString &output)
{
    if (m_batchTask)
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("A batch analysis is already running."));
        return;
    }

    ensureBatchProgressDialog();
    m_batchProgress->setValue(0);
    m_batchProgress->show();

    struct State
    {
        std::mutex mutex;
        QString output;
        std::string analyzerId;
        QStringList paths;
        std::string body;
        size_t resultCount = 0;
        bool cancelled = false;
        bool writeOk = false;
    };
    const auto state = std::make_shared<State>();
    state->output = output;
    state->analyzerId = analyzerId;
    state->paths = paths;
    const QPointer<ThumbnailPanel> guard(this);
    const bool asJson = output.endsWith(".json", Qt::CaseInsensitive);

    m_batchTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [state, asJson](const TaskScheduler::TaskContext &ctx)
        {
            std::vector<mviewer::analyzer::AnalyzerResult> results;
            results.reserve(static_cast<size_t>(state->paths.size()));
            AnalyzerRegistry &registry = AnalyzerRegistry::instance();
            for (int i = 0; i < state->paths.size(); ++i)
            {
                if (ctx.isCancelled())
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->cancelled = true;
                    return;
                }

                const QString &path = state->paths.at(i);
                const auto loaded = ImageRepository::instance().load(path.toStdString());
                // `loaded` and the analyzer are iteration-local. No vector of
                // full-resolution frames is retained across the batch.
                if (loaded.frame)
                {
                    auto analyzer = registry.create(state->analyzerId);
                    if (analyzer && analyzer->analyze(*loaded.frame))
                        results.push_back({path.toStdString(), analyzer->resultMetrics(),
                                           analyzer->resultText()});
                }
                const_cast<TaskScheduler::TaskContext &>(ctx).reportProgress(
                    (i + 1) * 100 / qMax(1, state->paths.size()));
            }
            if (ctx.isCancelled())
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->cancelled = true;
                return;
            }
            const auto report = mviewer::core::buildBatchReport(state->analyzerId, results);
            const size_t resultCount = results.size();
            const std::string body = asJson ? report.toJson() : report.toCsv();
            const bool writeOk =
                mviewer::exportjob::writeTextAtomically(state->output.toUtf8().toStdString(), body);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->resultCount = resultCount;
            state->body = body;
            state->writeOk = writeOk;
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state]()
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, state]()
                {
                    if (!guard)
                        return;
                    bool cancelled = false;
                    bool writeOk = false;
                    size_t resultCount = 0;
                    QString output;
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        cancelled = state->cancelled;
                        writeOk = state->writeOk;
                        resultCount = state->resultCount;
                        output = state->output;
                    }
                    guard->finishBatchAnalyzeExport(cancelled, writeOk, resultCount, output);
                },
                Qt::QueuedConnection);
        },
        [guard](int progress)
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, progress]()
                {
                    if (guard && guard->m_batchProgress)
                        guard->m_batchProgress->setValue(progress);
                },
                Qt::QueuedConnection);
        });

    if (!m_batchTask)
    {
        m_batchProgress->close();
        QMessageBox::warning(this, tr("Batch Analysis Export"),
                             tr("Background task was rejected by the scheduler."));
    }
}

void ThumbnailPanel::requestCompare()
{
    const QStringList sel = selectedPaths();
    if (sel.size() >= 2 && sel.size() <= 8)
        emit compareRequested(sel);
}
