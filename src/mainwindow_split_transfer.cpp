// Split-browse copy and move of the active pane selection into the other pane.
#include "mainwindow_p.h"
#include "splitbrowse_transfer.h"
#include "thumbnailpanel_transfer.h"

#include <QAction>

namespace
{
void setTransferAction(QAction *action, bool enabled, const QString &sameDirectoryTip,
                       const QString &idleTip)
{
    if (!action)
        return;
    action->setEnabled(enabled);
    action->setToolTip(enabled ? idleTip : sameDirectoryTip);
}
} // namespace

void MainWindow::syncOtherPaneDirectories()
{
    const QString left = m_thumbnailPanel ? m_thumbnailPanel->currentDir() : QString();
    const QString right =
        m_secondaryThumbnailPanel ? m_secondaryThumbnailPanel->currentDir() : QString();
    if (m_thumbnailPanel)
        m_thumbnailPanel->setProperty("otherPaneDirectory", right);
    if (m_secondaryThumbnailPanel)
        m_secondaryThumbnailPanel->setProperty("otherPaneDirectory", left);
    updateSwapButtonEnabled();
    updateOtherPaneTransferActions();
}

void MainWindow::updateOtherPaneTransferActions()
{
    ThumbnailPanel *panel = activeThumbnailPanel();
    const int count = panel ? panel->selectedPaths().size() : 0;
    const QString activeDir = panel ? panel->currentDir() : QString();
    const QString other = panel ? panel->property("otherPaneDirectory").toString() : QString();
    const auto decision = decideOtherPaneTransfer(m_splitBrowseEnabled, count, activeDir, other);
    const bool available = decision == OtherPaneTransferDecision::Available;
    const QString sameDir = decision == OtherPaneTransferDecision::SameDirectory
                                ? samePaneDirectoryMessage()
                                : QString();
    setTransferAction(m_actCopyOtherPane, available, sameDir,
                      tr("Copy the active pane selection to the other pane"));
    setTransferAction(m_actMoveOtherPane, available, sameDir,
                      tr("Move the active pane selection to the other pane"));
}

void MainWindow::copyActiveSelectionToOtherPane()
{
    if (ThumbnailPanel *panel = activeThumbnailPanel())
        ThumbnailPanelTransfer::copySelectedToOtherPane(*panel);
}

void MainWindow::moveActiveSelectionToOtherPane()
{
    if (ThumbnailPanel *panel = activeThumbnailPanel())
        ThumbnailPanelTransfer::moveSelectedToOtherPane(*panel);
}

void MainWindow::onFilesTransferred(const QString &destinationDirectory,
                                    const QStringList &createdPaths)
{
    if (m_thumbnailPanel)
        ThumbnailPanelTransfer::showTransferredFiles(*m_thumbnailPanel, destinationDirectory,
                                                     createdPaths);
    if (m_secondaryThumbnailPanel)
        ThumbnailPanelTransfer::showTransferredFiles(*m_secondaryThumbnailPanel,
                                                     destinationDirectory, createdPaths);
    updateUndoRedoActions();
}
