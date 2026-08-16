// ThumbnailPanel file operations: rename, trash, copy/move, batch export, context menu (M20 P0#3).
#include "thumbnailpanel_p.h"

#include "runtime_storage.h"

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
    const QString newPath = fi.absolutePath() + "/" + newName;

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
        setDirectory(m_currentDir);
        // M24 (A#8): keep the renamed file selected — Explorer/FastStone
        // parity. Without this the rescan drops the selection entirely.
        selectPath(newPath);
    }
}

void ThumbnailPanel::moveToTrashSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    // Qt6 removed QStandardPaths::TrashLocation; emulate a per-user trash dir.
    const QString dataDir =
        mviewer::runtime::writableDirectory(QStandardPaths::GenericDataLocation);
    const QString trashDir = dataDir.isEmpty() ? QString() : QDir(dataDir).filePath("trash");
    if (trashDir.isEmpty() || !QDir().mkpath(trashDir))
    {
        QMessageBox::warning(this, tr("删除失败"), tr("无法创建回收目录。"));
        return;
    }

    QStringList removed;
    // A-10: reversible delete via CommandStack when available.
    if (m_cmdStack)
    {
        std::vector<std::string> stdPaths;
        stdPaths.reserve(static_cast<size_t>(paths.size()));
        for (const QString &p : paths)
            stdPaths.push_back(p.toUtf8().toStdString());
        auto cmd = std::make_unique<FileDeleteCommand>(std::move(stdPaths), trashDir.toStdString());
        // Capture moved paths before ownership transfers.
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "删除失败", QString::fromStdString(m_cmdStack->lastError()));
        }
        for (const QString &p : paths)
            if (!QFileInfo::exists(p))
                removed.append(p);
    }
    else
    {
        auto cmd = std::make_unique<FileDeleteCommand>(toStdPaths(paths), trashDir.toUtf8().toStdString());
        cmd->execute();
        if (!cmd->lastError().empty())
            QMessageBox::warning(this, tr("删除失败"), QString::fromUtf8(cmd->lastError().c_str()));
        for (const QString &p : paths)
            if (!QFileInfo::exists(p))
                removed.append(p);
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
    // Let the host advance the viewer if the current image was just deleted.
    if (!removed.isEmpty())
        emit pathsRemoved(removed);
}

void ThumbnailPanel::copySelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "复制到...");
    if (dir.isEmpty())
        return;
    const auto fileSystem = mviewer::core::defaultFileSystemAdapter();
    size_t copied = 0;
    QStringList failures;
    for (const QString &p : paths)
    {
        const auto source = mviewer::core::pathFromUtf8(p.toUtf8().toStdString());
        std::string destinationError;
        const auto destination = mviewer::core::collisionFreeDestination(
            source, mviewer::core::pathFromUtf8(dir.toUtf8().toStdString()), fileSystem,
            destinationError);
        if (destination.empty())
        {
            failures.append(p + ": " + QString::fromUtf8(destinationError.c_str()));
            continue;
        }
        const auto result = mviewer::core::copyFileAtomically(source, destination, fileSystem);
        if (result.state == mviewer::core::FileTransferState::Succeeded)
            ++copied;
        else
            failures.append(p + ": " + QString::fromUtf8(result.error.c_str()));
    }
    const QString summary = tr("复制完成：成功 %1，失败 %2。%3")
                                 .arg(static_cast<qlonglong>(copied))
                                 .arg(failures.size())
                                 .arg(failures.isEmpty() ? QString() : "\n" + failures.join("\n"));
    if (failures.isEmpty())
        QMessageBox::information(this, tr("复制"), summary);
    else
        QMessageBox::warning(this, tr("复制"), summary);
}

void ThumbnailPanel::moveSelectedTo()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, "移动到...");
    if (dir.isEmpty())
        return;

    // A-10: reversible move via CommandStack when available.
    if (m_cmdStack)
    {
        std::vector<std::string> stdPaths;
        stdPaths.reserve(static_cast<size_t>(paths.size()));
        for (const QString &p : paths)
            stdPaths.push_back(p.toUtf8().toStdString());
        auto cmd = std::make_unique<FileMoveCommand>(std::move(stdPaths), dir.toUtf8().toStdString());
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "移动失败", QString::fromStdString(m_cmdStack->lastError()));
            return;
        }
    }
    else
    {
        auto cmd = std::make_unique<FileMoveCommand>(toStdPaths(paths), dir.toUtf8().toStdString());
        cmd->execute();
        if (!cmd->lastError().empty())
            QMessageBox::warning(this, tr("移动失败"), QString::fromUtf8(cmd->lastError().c_str()));
    }
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
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
            const bool writeOk = mviewer::exportjob::writeTextAtomically(
                state->output.toUtf8().toStdString(), body);
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
                        QMessageBox::information(
                            guard, QObject::tr("批量分析导出"),
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
    QAction *aTrash = menu.addAction("移到回收站");
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
