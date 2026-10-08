#include "thumbnailpanel_p.h"

struct ThumbnailPanel::ContextMenuActions
{
    QAction *aOpen = nullptr;
    QAction *aRename = nullptr;
    QAction *aCopy = nullptr;
    QAction *aMove = nullptr;
    QAction *aTrash = nullptr;
    QAction *aReveal = nullptr;
    QAction *aCopyPath = nullptr;
    QAction *aCopyName = nullptr;
    QAction *aCompare = nullptr;
    QAction *aAnalyze = nullptr;
    QAction *aAddTag = nullptr;
    QMenu *rmTagMenu = nullptr;
};

namespace
{
void populateRemoveTagMenu(QMenu *rmTagMenu, const QStringList &selPaths)
{
    QStringList tags;
    for (const QString &sp : selPaths)
    {
        for (const auto &tg : mviewer::core::TagStore::instance().tags(sp.toStdString()))
        {
            if (const QString text = QString::fromStdString(tg); !tags.contains(text))
                tags.append(text);
        }
    }
    tags.sort();
    for (const QString &tg : tags)
        rmTagMenu->addAction(tg);
    rmTagMenu->setEnabled(!tags.isEmpty());
}
} // namespace

ThumbnailPanel::ContextMenuActions ThumbnailPanel::buildContextMenu(QMenu &menu,
                                                                    const QStringList &selPaths)
{
    ContextMenuActions acts;
    acts.aOpen = menu.addAction("打开");
    acts.aOpen->setShortcut(QKeySequence(Qt::Key_Return));
    acts.aRename = menu.addAction("重命名");
    acts.aRename->setShortcut(QKeySequence(Qt::Key_F2));
    // NOTE: no Ctrl+C here — the global binding copies the current image to
    // the clipboard; advertising the file-copy dialog on the same key would
    // be misleading (the dialog stays reachable via the menu item).
    acts.aCopy = menu.addAction("复制...");
    acts.aMove = menu.addAction("移动...");
    acts.aMove->setShortcut(QKeySequence("Ctrl+M"));
    acts.aTrash = menu.addAction("移到 MViewer 回收站");
    acts.aTrash->setShortcut(QKeySequence(Qt::Key_Delete));
    acts.aReveal = menu.addAction("在资源管理器中显示");
    acts.aReveal->setShortcut(QKeySequence("Ctrl+E"));
    acts.aCopyPath = menu.addAction("复制路径");
    acts.aCopyPath->setShortcut(QKeySequence("Ctrl+Shift+C"));
    acts.aCopyName = menu.addAction("复制文件名");
    acts.aCompare = menu.addAction("比较");
    acts.aCompare->setShortcut(QKeySequence("P"));
    acts.aCompare->setEnabled(selPaths.size() >= 2 && selPaths.size() <= 8);
    acts.aAnalyze = menu.addAction("批量分析导出");
    acts.aAnalyze->setShortcut(QKeySequence("Ctrl+Alt+A"));
    acts.aAnalyze->setEnabled(!selPaths.isEmpty());
    menu.addSeparator();
    acts.aAddTag = menu.addAction("添加标签…");
    acts.aAddTag->setEnabled(!selPaths.isEmpty());
    acts.rmTagMenu = menu.addMenu("移除所选标签");
    populateRemoveTagMenu(acts.rmTagMenu, selPaths);
    return acts;
}

void ThumbnailPanel::addTagToSelected()
{
    bool ok = false;
    const QString tag = QInputDialog::getText(this, tr("添加标签"), tr("给所选图片添加标签："),
                                              QLineEdit::Normal, QString(), &ok);
    if (ok && !tag.trimmed().isEmpty())
    {
        const std::string tagStd = tag.trimmed().toStdString();
        for (const QString &sp : selectedPaths())
            mviewer::core::TagStore::instance().addTag(sp.toStdString(), tagStd);
        applyFilter();
        viewport()->update();
    }
}

void ThumbnailPanel::removeTagFromSelected(const QString &tag)
{
    const std::string tagStd = tag.toStdString();
    for (const QString &sp : selectedPaths())
        mviewer::core::TagStore::instance().removeTag(sp.toStdString(), tagStd);
    applyFilter();
    viewport()->update();
}

void ThumbnailPanel::handleContextMenuAction(const ContextMenuActions &actions, QAction *chosen,
                                             const QString &path)
{
    if (chosen == actions.aOpen)
        emit itemDoubleClicked(path);
    else if (chosen == actions.aRename)
        renameSelected();
    else if (chosen == actions.aCopy)
        copySelectedTo();
    else if (chosen == actions.aMove)
        moveSelectedTo();
    else if (chosen == actions.aTrash)
        moveToTrashSelected();
    else if (chosen == actions.aReveal)
        revealSelected();
    else if (chosen == actions.aCopyPath)
        copySelectedPaths();
    else if (chosen == actions.aCopyName)
        copySelectedFileNames();
    else if (chosen == actions.aCompare)
        requestCompare();
    else if (chosen == actions.aAnalyze)
        batchAnalyzeExport();
    else if (chosen == actions.aAddTag)
        addTagToSelected();
    else if (actions.rmTagMenu && actions.rmTagMenu->actions().contains(chosen))
        removeTagFromSelected(chosen->text());
}

void ThumbnailPanel::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;
    const QString path = m_paths.value(idx.row());
    if (!selectionModel()->isSelected(idx))
        selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Clear);

    const QStringList selPaths = selectedPaths();
    QMenu menu(this);
    const ContextMenuActions actions = buildContextMenu(menu, selPaths);
    populateRatingContextMenu(&menu);
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    handleContextMenuAction(actions, chosen, path);
}
