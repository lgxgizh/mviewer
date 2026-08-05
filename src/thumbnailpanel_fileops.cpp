// ThumbnailPanel file operations: rename, trash, copy/move, batch export, context menu (M20 P0#3).
#include "thumbnailpanel_p.h"

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
        auto cmd =
            std::make_unique<FileRenameCommand>(oldPath.toStdString(), newPath.toStdString());
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "重命名失败",
                                 QString::fromStdString(m_cmdStack->lastError()));
            return;
        }
    }
    else if (!QFile::rename(oldPath, newPath))
    {
        return;
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
    const QString trashDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mviewer/trash";
    QDir().mkpath(trashDir);

    QStringList removed;
    // A-10: reversible delete via CommandStack when available.
    if (m_cmdStack)
    {
        std::vector<std::string> stdPaths;
        stdPaths.reserve(static_cast<size_t>(paths.size()));
        for (const QString &p : paths)
            stdPaths.push_back(p.toStdString());
        auto cmd = std::make_unique<FileDeleteCommand>(std::move(stdPaths), trashDir.toStdString());
        // Capture moved paths before ownership transfers.
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "删除失败", QString::fromStdString(m_cmdStack->lastError()));
            return;
        }
        // After successful execute, files are gone — treat all selected as removed.
        removed = paths;
    }
    else
    {
        for (const QString &p : paths)
        {
            if (QFile::rename(p, trashDir + "/" + QFileInfo(p).fileName()))
                removed.append(p);
            else if (QFile::remove(p))
                removed.append(p);
        }
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
    for (const QString &p : paths)
        QFile::copy(p, dir + "/" + QFileInfo(p).fileName());
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
            stdPaths.push_back(p.toStdString());
        auto cmd = std::make_unique<FileMoveCommand>(std::move(stdPaths), dir.toStdString());
        if (!m_cmdStack->execute(std::move(cmd)))
        {
            QMessageBox::warning(this, "移动失败", QString::fromStdString(m_cmdStack->lastError()));
            return;
        }
    }
    else
    {
        for (const QString &p : paths)
            QFile::rename(p, dir + "/" + QFileInfo(p).fileName());
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

    QApplication::setOverrideCursor(Qt::BusyCursor);
    const auto cursorGuard = qScopeGuard([] { QApplication::restoreOverrideCursor(); });

    std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> frames;
    frames.reserve(static_cast<size_t>(paths.size()));
    for (const QString &p : paths)
    {
        auto res = ImageRepository::instance().load(p.toStdString());
        if (res.frame)
            frames.emplace_back(p.toStdString(), res.frame);
    }
    if (frames.empty())
    {
        QMessageBox::warning(this, tr("批量分析导出"), tr("所选图片均无法解码。"));
        return;
    }

    const auto results = reg.runBatch(frames, analyzerId);
    const auto report = mviewer::core::buildBatchReport(analyzerId, results);
    const bool asJson = out.endsWith(".json", Qt::CaseInsensitive);
    const std::string body = asJson ? report.toJson() : report.toCsv();

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, tr("批量分析导出"), tr("无法写入：%1").arg(out));
        return;
    }
    f.write(QByteArray::fromStdString(body));
    f.close();
    QMessageBox::information(this, tr("批量分析导出"),
                             tr("已导出 %1 条结果 → %2").arg(results.size()).arg(out));
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
    QAction *aCopy = menu.addAction("复制...");
    aCopy->setShortcut(QKeySequence("Ctrl+C"));
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
