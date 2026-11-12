#include "browse_file_clipboard.h"
#include "splitbrowse_transfer.h"
#include "thumbnailpanel_p.h"
#include "thumbnailpanel_testaccess.h"
#include "thumbnailpanel_transfer.h"

#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/yuv/YuvFile.h"

#include <algorithm>

struct ThumbnailPanel::ContextMenuActions
{
    QAction *aOpen = nullptr;
    QAction *aRename = nullptr;
    QAction *aCopyFiles = nullptr;
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
    QAction *aYuvParams = nullptr;  // only for a single .yuv/.y4m selection
    QAction *aYuvCompare = nullptr; // only for exactly two .yuv/.y4m files
    QStringList yuvPaths;
    QAction *aAnalyze = nullptr;
};

ThumbnailPanel::ContextMenuActions ThumbnailPanel::buildContextMenu(QMenu &menu,
                                                                    const QStringList &selPaths)
{
    ContextMenuActions acts;
    acts.aOpen = menu.addAction(tr("Open"));
    acts.aOpen->setShortcut(QKeySequence(Qt::Key_Return));
    acts.aRename = menu.addAction(tr("Rename"));
    acts.aRename->setShortcut(QKeySequence(Qt::Key_F2));
    // Copy = Ctrl+C: the selected files go on the clipboard for Explorer's
    // Ctrl+V (MainWindow routes the key to the same payload). "Copy To..."
    // is the folder-picker transfer.
    acts.aCopyFiles = menu.addAction(tr("Copy"));
    acts.aCopyFiles->setShortcut(QKeySequence(QKeySequence::Copy));
    acts.aCopy = menu.addAction(tr("Copy To..."));
    acts.aMove = menu.addAction(tr("Move..."));
    acts.aMove->setShortcut(QKeySequence("Ctrl+M"));
    const TransferShortcut copyOther = copyToOtherPaneShortcut();
    const TransferShortcut moveOther = moveToOtherPaneShortcut();
    acts.aCopyOther = menu.addAction(tr("Copy to Other Pane"));
    acts.aCopyOther->setShortcut(QKeySequence(copyOther.key | copyOther.mods));
    acts.aMoveOther = menu.addAction(tr("Move to Other Pane"));
    acts.aMoveOther->setShortcut(QKeySequence(moveOther.key | moveOther.mods));
    const bool split = m_selection && m_selection->isSplitModeEnabled();
    const QString otherDir = property("otherPaneDirectory").toString();
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
    acts.aOpenTrash = menu.addAction(tr("Open Recycle Bin"));
    acts.aReveal = menu.addAction(tr("Show in File Manager"));
    acts.aReveal->setShortcut(QKeySequence("Ctrl+E"));
    acts.aCopyPath = menu.addAction(tr("Copy path"));
    acts.aCopyPath->setShortcut(QKeySequence("Ctrl+Shift+C"));
    acts.aCopyName = menu.addAction(tr("Copy filename"));
    acts.aCompare = menu.addAction(tr("Compare"));
    acts.aCompare->setShortcut(QKeySequence("P"));
    const auto compareCount = compareSelection().size();
    acts.aCompare->setEnabled(compareCount >= 2 && compareCount <= 8);
    acts.aUsmCompare = menu.addAction(tr("USM Sharpen Compare..."));
    acts.aUsmCompare->setEnabled(selPaths.size() == 1);
    if (std::all_of(selPaths.begin(), selPaths.end(), [](const QString &p)
                    { return mviewer::core::yuv::isYuvPath(p.toUtf8().toStdString()); }))
    {
        acts.yuvPaths = selPaths;
        if (selPaths.size() == 1)
            acts.aYuvParams = menu.addAction(tr("YUV Parameters..."));
        else if (selPaths.size() == 2)
            acts.aYuvCompare = menu.addAction(tr("Compare YUV (PSNR / SSIM)..."));
    }
    else if (selPaths.size() == 1 &&
             mviewer::core::rawplain::isPlainRawPath(selPaths.front().toUtf8().toStdString()))
    {
        // ADR 033/034: same signal as the YUV dialog; MainWindow routes by suffix.
        acts.yuvPaths = selPaths;
        const bool gray =
            mviewer::core::rawplain::isGrayRawPath(selPaths.front().toUtf8().toStdString());
        acts.aYuvParams = menu.addAction(gray ? tr("Gray Parameters...") : tr("RAW Parameters..."));
    }
    acts.aAnalyze = menu.addAction(tr("Batch Analysis Export"));
    acts.aAnalyze->setShortcut(QKeySequence("Ctrl+Alt+A"));
    acts.aAnalyze->setEnabled(!selPaths.isEmpty());
    return acts;
}

QStringList ThumbnailPanelTestAccess::browseContextMenuTitles(ThumbnailPanel &panel,
                                                              const QStringList &selPaths)
{
    QMenu menu;
    panel.buildContextMenu(menu, selPaths);
    QStringList titles;
    for (QAction *action : menu.actions())
    {
        titles.append(action->text());
        if (action->menu())
        {
            for (QAction *child : action->menu()->actions())
                titles.append(child->text());
        }
    }
    return titles;
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
    if (chosen == actions.aCopyFiles)
    {
        const QStringList files = mviewer::browseclip::copyTargets(compareSelection(), path);
        if (!files.isEmpty())
            QApplication::clipboard()->setMimeData(
                mviewer::browseclip::makeFileCopyMimeData(files));
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
    else if (chosen != nullptr && (chosen == actions.aYuvParams || chosen == actions.aYuvCompare))
        emit yuvToolRequested(actions.yuvPaths);
    else if (chosen == actions.aAnalyze)
        batchAnalyzeExport();
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
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    handleContextMenuAction(actions, chosen, path);
}
