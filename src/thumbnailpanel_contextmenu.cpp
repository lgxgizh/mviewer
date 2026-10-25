#include "splitbrowse_transfer.h"
#include "thumbnailpanel_p.h"
#include "thumbnailpanel_transfer.h"

struct ThumbnailPanel::ContextMenuActions
{
    QAction *aOpen = nullptr;
    QAction *aRename = nullptr;
    QAction *aCopy = nullptr;
    QAction *aMove = nullptr;
    QAction *aCopyOther = nullptr;
    QAction *aMoveOther = nullptr;
    QAction *aTrash = nullptr;
    QAction *aOpenTrash = nullptr;
    QAction *aReveal = nullptr;
    QAction *aCopyPath = nullptr;
    QAction *aCopyName = nullptr;
    QAction *aCompare = nullptr;
    QAction *aUsmCompare = nullptr;
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
    acts.aOpen = menu.addAction(tr("Open"));
    acts.aOpen->setShortcut(QKeySequence(Qt::Key_Return));
    acts.aRename = menu.addAction("Rename");
    acts.aRename->setShortcut(QKeySequence(Qt::Key_F2));
    // NOTE: no Ctrl+C here — the global binding copies the current image to
    // the clipboard; advertising the file-copy dialog on the same key would
    // be misleading (the dialog stays reachable via the menu item).
    acts.aCopy = menu.addAction(tr("Copy..."));
    acts.aMove = menu.addAction(tr("Move..."));
    acts.aMove->setShortcut(QKeySequence("Ctrl+M"));
    const TransferShortcut copyOther = copyToOtherPaneShortcut();
    const TransferShortcut moveOther = moveToOtherPaneShortcut();
    acts.aCopyOther = menu.addAction(tr("Copy to Other Pane"));
    acts.aCopyOther->setShortcut(QKeySequence(copyOther.key | copyOther.mods));
    acts.aMoveOther = menu.addAction(tr("Move to Other Pane"));
    acts.aMoveOther->setShortcut(QKeySequence(moveOther.key | moveOther.mods));
    const bool split = m_selection && m_selection->splitModeEnabled();
    const QString otherDir = property(QStringLiteral("otherPaneDirectory")).toString();
    const auto decision = decideOtherPaneTransfer(split, selPaths.size(), m_currentDir, otherDir);
    const bool canTransfer = decision == OtherPaneTransferDecision::Available;
    acts.aCopyOther->setEnabled(canTransfer);
    acts.aMoveOther->setEnabled(canTransfer);
    if (decision == OtherPaneTransferDecision::SameDirectory)
    {
        const QString sameDir = samePaneDirectoryMessage();
        acts.aCopyOther->setToolTip(sameDir);
        acts.aMoveOther->setToolTip(sameDir);
    }
    acts.aTrash = menu.addAction(tr("Move to Recycle Bin"));
    acts.aTrash->setShortcut(QKeySequence(Qt::Key_Delete));
    acts.aOpenTrash = menu.addAction("Open Recycle Bin");
    acts.aReveal = menu.addAction(tr("Show in File Manager"));
    acts.aReveal->setShortcut(QKeySequence("Ctrl+E"));
    acts.aCopyPath = menu.addAction("Copy path");
    acts.aCopyPath->setShortcut(QKeySequence("Ctrl+Shift+C"));
    acts.aCopyName = menu.addAction("Copy filename");
    acts.aCompare = menu.addAction("Compare");
    acts.aCompare->setShortcut(QKeySequence("P"));
    acts.aCompare->setEnabled(selPaths.size() >= 2 && selPaths.size() <= 8);
    acts.aUsmCompare = menu.addAction(tr("USM Sharpen Compare..."));
    acts.aUsmCompare->setEnabled(selPaths.size() == 1);
    acts.aAnalyze = menu.addAction("Batch Analysis Export");
    acts.aAnalyze->setShortcut(QKeySequence("Ctrl+Alt+A"));
    acts.aAnalyze->setEnabled(!selPaths.isEmpty());
    menu.addSeparator();
    acts.aAddTag = menu.addAction(tr("Add Tag…"));
    acts.aAddTag->setEnabled(!selPaths.isEmpty());
    acts.rmTagMenu = menu.addMenu(tr("Remove Tag from Selection"));
    populateRemoveTagMenu(acts.rmTagMenu, selPaths);
    return acts;
}

void ThumbnailPanel::addTagToSelected()
{
    bool ok = false;
    const QString tag =
        QInputDialog::getText(this, tr("Add tag"), tr("Add a tag to the selected images:"),
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

bool ThumbnailPanel::handleFileContextMenuAction(const ContextMenuActions &actions, QAction *chosen,
                                                 const QString &path)
{
    if (chosen == actions.aOpen)
    {
        emit itemDoubleClicked(path);
        return true;
    }
    if (chosen == actions.aRename)
    {
        renameSelected();
        return true;
    }
    if (chosen == actions.aCopy)
    {
        copySelectedTo();
        return true;
    }
    if (chosen == actions.aMove)
    {
        moveSelectedTo();
        return true;
    }
    if (chosen == actions.aCopyOther)
    {
        ThumbnailPanelTransfer::copySelectedToOtherPane(*this);
        return true;
    }
    if (chosen == actions.aMoveOther)
    {
        ThumbnailPanelTransfer::moveSelectedToOtherPane(*this);
        return true;
    }
    if (chosen == actions.aTrash)
    {
        moveToTrashSelected();
        return true;
    }
    if (chosen == actions.aOpenTrash)
    {
        openTrashFolder();
        return true;
    }
    if (chosen == actions.aReveal)
    {
        revealSelected();
        return true;
    }
    if (chosen == actions.aCopyPath)
    {
        copySelectedPaths();
        return true;
    }
    if (chosen == actions.aCopyName)
    {
        copySelectedFileNames();
        return true;
    }
    return false;
}

void ThumbnailPanel::handleContextMenuAction(const ContextMenuActions &actions, QAction *chosen,
                                             const QString &path)
{
    if (handleFileContextMenuAction(actions, chosen, path))
        return;
    if (chosen == actions.aCompare)
        requestCompare();
    else if (chosen == actions.aUsmCompare)
        emit usmCompareRequested(path);
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
