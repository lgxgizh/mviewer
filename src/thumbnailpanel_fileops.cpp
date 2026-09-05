// ThumbnailPanel file operations: rename, trash, copy/move, batch export, context menu (M20 P0#3).
#include "thumbnailpanel_p.h"

#include "runtime_storage.h"

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

struct AsyncCopyState
{
    int copied = 0;
    bool cancelled = false;
    QStringList failures;
};

int transferPercent(uintmax_t copied, uintmax_t total)
{
    if (total == 0)
        return copied == 0 ? 0 : 100;
    const auto ratio = static_cast<double>(copied) / static_cast<double>(total);
    return std::clamp(static_cast<int>(ratio * 100.0), 0, 100);
}
} // namespace

void ThumbnailPanel::startCommandFileOperation(std::unique_ptr<ICommand> command,
                                               const QStringList &paths, const QString &label)
{
    if (!command || paths.isEmpty() || m_fileOperationBusy)
        return;

    m_fileOperationBusy = true;
    const uint64_t generation = ++m_fileOperationGeneration;
    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> guard(this);
    auto state = std::make_shared<AsyncCommandState>(std::move(command));

    m_fileProgress =
        new QProgressDialog(label + QStringLiteral("…"), QStringLiteral("取消"), 0, 100, this);
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

    auto lastProgress = std::make_shared<std::atomic<int>>(-1);
    const auto onProgress = [guard, alive, generation, lastProgress](int value)
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
            if (auto *move = dynamic_cast<FileMoveCommand *>(state->command.get()))
                move->setTransferObserver(observer);
            else if (auto *del = dynamic_cast<FileDeleteCommand *>(state->command.get()))
                del->setTransferObserver(observer);

            try
            {
                state->command->execute();
            }
            catch (...)
            {
                if (auto *move = dynamic_cast<FileMoveCommand *>(state->command.get()))
                    move->setTransferObserver({});
                else if (auto *del = dynamic_cast<FileDeleteCommand *>(state->command.get()))
                    del->setTransferObserver({});
                throw;
            }
            // The command is retained for Undo/Redo. Do not retain the worker
            // callback, whose TaskContext is only valid for this execution.
            if (auto *move = dynamic_cast<FileMoveCommand *>(state->command.get()))
                move->setTransferObserver({});
            else if (auto *del = dynamic_cast<FileDeleteCommand *>(state->command.get()))
                del->setTransferObserver({});
            state->succeeded = state->command->lastError().empty();
            state->cancelled = ctx.isCancelled();
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, generation, state, paths, label]() mutable
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, generation, state, paths, label]() mutable
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
                        state->command ? state->command->lastError() : "Command was lost.";
                    const bool unresolved = state->command && state->command->hasUnresolvedState();
                    if (panel->m_cmdStack)
                        panel->m_cmdStack->recordExecuted(std::move(state->command));

                    if (!state->succeeded)
                    {
                        const QString detail = QString::fromUtf8(error.c_str());
                        QMessageBox::warning(panel, label,
                                             state->cancelled
                                                 ? label + QStringLiteral("已取消。\n") + detail
                                                 : label + QStringLiteral("失败。\n") + detail);
                    }

                    if (!panel->m_currentDir.isEmpty())
                    {
                        if (panel->m_liveDirectoryMonitoring)
                            emit panel->directoryContentsChanged(panel->m_currentDir);
                        else
                            panel->refresh();
                    }

                    QStringList removed;
                    for (const QString &path : paths)
                    {
                        if (!QFileInfo::exists(path))
                            removed.append(path);
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
    {
        m_fileOperationBusy = false;
        if (m_fileProgress)
        {
            m_fileProgress->close();
            m_fileProgress->deleteLater();
            m_fileProgress = nullptr;
        }
        QMessageBox::warning(this, label, label + QStringLiteral("无法排队：后台任务队列已满。"));
    }
}

void ThumbnailPanel::startCopyFileOperation(const QStringList &paths,
                                            const QString &destinationDirectory)
{
    if (paths.isEmpty() || destinationDirectory.isEmpty() || m_fileOperationBusy)
        return;

    m_fileOperationBusy = true;
    const uint64_t generation = ++m_fileOperationGeneration;
    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> guard(this);
    auto state = std::make_shared<AsyncCopyState>();
    const auto fileSystem = mviewer::core::defaultFileSystemAdapter();

    m_fileProgress =
        new QProgressDialog(QStringLiteral("复制中…"), QStringLiteral("取消"), 0, 100, this);
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

    auto lastProgress = std::make_shared<std::atomic<int>>(-1);
    const auto onProgress = [guard, alive, generation, lastProgress](int value)
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
                                           QString::fromUtf8(destinationError.c_str()));
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
                    state->failures.append(path + ": " + QString::fromUtf8(result.error.c_str()));
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
                        QStringLiteral("复制完成：成功 %1，失败 %2。%3")
                            .arg(state->copied)
                            .arg(state->failures.size())
                            .arg(state->failures.isEmpty()
                                     ? QString()
                                     : QStringLiteral("\n") + state->failures.join("\n"));
                    if (state->cancelled)
                        QMessageBox::warning(panel, QStringLiteral("复制"),
                                             QStringLiteral("复制已取消。\n") + summary);
                    else if (state->failures.isEmpty())
                        QMessageBox::information(panel, QStringLiteral("复制"), summary);
                    else
                        QMessageBox::warning(panel, QStringLiteral("复制"), summary);
                },
                Qt::QueuedConnection);
        },
        onProgress);

    if (!m_fileOperationTask)
    {
        m_fileOperationBusy = false;
        if (m_fileProgress)
        {
            m_fileProgress->close();
            m_fileProgress->deleteLater();
            m_fileProgress = nullptr;
        }
        QMessageBox::warning(this, QStringLiteral("复制"),
                             QStringLiteral("复制无法排队：后台任务队列已满。"));
    }
}

void ThumbnailPanel::renameSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString oldPath = paths.first();
    const QFileInfo fi(oldPath);
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, "重命名", "新文件名:", QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.isEmpty() || newName == fi.fileName())
        return;
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')) ||
        QFileInfo(newName).fileName() != newName)
    {
        QMessageBox::warning(this, QStringLiteral("重命名失败"),
                             QStringLiteral("文件名不能包含路径。"));
        return;
    }
    const QString newPath = fi.absolutePath() + "/" + newName;
    if (QFileInfo(newPath).absolutePath() != fi.absolutePath())
    {
        QMessageBox::warning(this, QStringLiteral("重命名失败"),
                             QStringLiteral("文件名不能包含路径。"));
        return;
    }

    // A-10: reversible rename via CommandStack when available.
    if (m_cmdStack)
    {
        auto cmd = std::make_unique<FileRenameCommand>(oldPath.toUtf8().toStdString(),
                                                       newPath.toUtf8().toStdString());
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "重命名失败",
                                 QString::fromStdString(m_cmdStack->lastError()));
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
            QMessageBox::warning(this, tr("重命名失败"),
                                 QString::fromUtf8(cmd->lastError().c_str()));
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
}

void ThumbnailPanel::moveToTrashSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    // Qt6 removed QStandardPaths::TrashLocation. Until a native Windows
    // Shell recycle-bin adapter is available, use and label an explicit
    // per-user MViewer trash staging area so users are not promised native
    // Recycle Bin semantics.
    const QString dataDir =
        mviewer::runtime::writableDirectory(QStandardPaths::GenericDataLocation);
    const QString trashDir = dataDir.isEmpty() ? QString() : QDir(dataDir).filePath("trash");
    if (trashDir.isEmpty() || !QDir().mkpath(trashDir))
    {
        QMessageBox::warning(this, tr("删除失败"), tr("无法创建回收目录。"));
        return;
    }

    auto cmd =
        std::make_unique<FileDeleteCommand>(toStdPaths(paths), trashDir.toUtf8().toStdString());
    startCommandFileOperation(std::move(cmd), paths, QStringLiteral("删除"));
}

void ThumbnailPanel::copySelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "复制到...");
    if (dir.isEmpty())
        return;
    startCopyFileOperation(paths, dir);
}

void ThumbnailPanel::moveSelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || m_fileOperationBusy)
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "移动到...");
    if (dir.isEmpty())
        return;

    auto cmd = std::make_unique<FileMoveCommand>(toStdPaths(paths), dir.toUtf8().toStdString());
    startCommandFileOperation(std::move(cmd), paths, QStringLiteral("移动"));
}

void ThumbnailPanel::revealSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString p = QDir::toNativeSeparators(paths.first());
#ifdef Q_OS_WIN
    QProcess::execute("explorer.exe", QStringList() << "/select," << p);
#else
    QProcess::execute("xdg-open", QStringList() << QFileInfo(paths.first()).absolutePath());
#endif
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
        QMessageBox::information(this, tr("批量分析导出"),
                                 tr("请先选择图片，或打开一个已过滤的目录。"));
        return;
    }

    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    const std::vector<std::string> ids = reg.availableAnalyzers();
    if (ids.empty())
    {
        QMessageBox::warning(this, tr("批量分析导出"), tr("当前没有可用的分析器。"));
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
    const QString chosen =
        QInputDialog::getItem(this, tr("批量分析导出"), tr("选择分析器:"), labels, 0, false, &ok);
    if (!ok || chosen.isEmpty())
        return;
    const int chosenIdx = labels.indexOf(chosen);
    if (chosenIdx < 0 || chosenIdx >= static_cast<int>(ids.size()))
        return;
    const std::string analyzerId = ids[static_cast<size_t>(chosenIdx)];

    const QString out = QFileDialog::getSaveFileName(this, tr("导出分析结果"), QString(),
                                                     tr("CSV (*.csv);;JSON (*.json)"));
    if (out.isEmpty())
        return;

    runBatchAnalyzeExportAsync(paths, analyzerId, out);
    return;
}

void ThumbnailPanel::runBatchAnalyzeExportAsync(const QStringList &paths,
                                                const std::string &analyzerId,
                                                const QString &output)
{
    if (m_batchTask)
    {
        QMessageBox::information(this, tr("批量分析导出"), tr("已有批量分析正在运行。"));
        return;
    }

    if (!m_batchProgress)
    {
        m_batchProgress = new QProgressDialog(tr("正在批量分析..."), tr("取消"), 0, 100, this);
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
                    guard->m_batchTask.reset();
                    if (guard->m_batchProgress)
                        guard->m_batchProgress->close();
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
                    if (cancelled)
                    {
                        QMessageBox::information(guard, QObject::tr("批量分析导出"),
                                                 QObject::tr("批量分析已取消。"));
                    }
                    else if (!writeOk)
                    {
                        QMessageBox::critical(guard, QObject::tr("批量分析导出"),
                                              QObject::tr("无法写入：%1").arg(state->output));
                    }
                    else
                    {
                        QMessageBox::information(guard, QObject::tr("批量分析导出"),
                                                 QObject::tr("已导出 %1 条结果 → %2")
                                                     .arg(static_cast<qlonglong>(resultCount))
                                                     .arg(output));
                    }
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
        QMessageBox::warning(this, tr("批量分析导出"), tr("后台任务被调度器拒绝。"));
    }
}

void ThumbnailPanel::onCompareClicked()
{
    const QStringList sel = selectedPaths();
    if (sel.size() >= 2 && sel.size() <= 8)
        emit compareRequested(sel);
}

void ThumbnailPanel::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;
    const QString path = m_paths.value(idx.row());
    if (!selectionModel()->isSelected(idx))
        selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Clear);

    QMenu menu(this);
    QAction *aOpen = menu.addAction("打开");
    aOpen->setShortcut(QKeySequence(Qt::Key_Return));
    QAction *aRename = menu.addAction("重命名");
    aRename->setShortcut(QKeySequence(Qt::Key_F2));
    // NOTE: no Ctrl+C here — the global binding copies the current image to
    // the clipboard; advertising the file-copy dialog on the same key would
    // be misleading (the dialog stays reachable via the menu item).
    QAction *aCopy = menu.addAction("复制...");
    QAction *aMove = menu.addAction("移动...");
    aMove->setShortcut(QKeySequence("Ctrl+M"));
    QAction *aTrash = menu.addAction("移到 MViewer 回收站");
    aTrash->setShortcut(QKeySequence(Qt::Key_Delete));
    QAction *aReveal = menu.addAction("在资源管理器中显示");
    aReveal->setShortcut(QKeySequence("Ctrl+E"));
    QAction *aCopyPath = menu.addAction("复制路径");
    aCopyPath->setShortcut(QKeySequence("Ctrl+Shift+C"));
    QAction *aCompare = menu.addAction("比较");
    QAction *aAnalyze = menu.addAction("批量分析导出");
    menu.addSeparator();
    QAction *aAddTag = menu.addAction("添加标签…");
    QMenu *rmTagMenu = menu.addMenu("移除标签");
    {
        const auto myTags = mviewer::core::TagStore::instance().tags(path.toStdString());
        for (const auto &tg : myTags)
            rmTagMenu->addAction(QString::fromStdString(tg));
        rmTagMenu->setEnabled(!myTags.empty());
    }
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == aOpen)
        emit itemDoubleClicked(path);
    else if (chosen == aRename)
        renameSelected();
    else if (chosen == aCopy)
        copySelectedTo();
    else if (chosen == aMove)
        moveSelectedTo();
    else if (chosen == aTrash)
        moveToTrashSelected();
    else if (chosen == aReveal)
        revealSelected();
    else if (chosen == aCopyPath)
        QApplication::clipboard()->setText(path);
    else if (chosen == aCompare)
        onCompareClicked();
    else if (chosen == aAnalyze)
        batchAnalyzeExport();
    else if (chosen == aAddTag)
    {
        bool ok = false;
        const QString tag = QInputDialog::getText(this, tr("添加标签"), tr("给所选图片添加标签："),
                                                  QLineEdit::Normal, QString(), &ok);
        if (ok && !tag.trimmed().isEmpty())
        {
            mviewer::core::TagStore::instance().addTag(path.toStdString(),
                                                       tag.trimmed().toStdString());
            applyFilter();
            viewport()->update();
        }
    }
    else if (rmTagMenu->actions().contains(chosen))
    {
        mviewer::core::TagStore::instance().removeTag(path.toStdString(),
                                                      chosen->text().toStdString());
        applyFilter();
        viewport()->update();
    }
}
