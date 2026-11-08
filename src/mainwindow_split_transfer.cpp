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
    const QList<ThumbnailPanel *> panels = {m_thumbnailPanel, m_secondaryThumbnailPanel,
                                            m_tertiaryThumbnailPanel};
    const int count = browseColumnCount();
    for (int source = 0; source < panels.size(); ++source)
    {
        ThumbnailPanel *panel = panels[source];
        if (!panel)
            continue;
        QStringList destinations;
        for (int target = 0; target < count; ++target)
        {
            if (target != source && panels[target] && !panels[target]->currentDir().isEmpty() &&
                !destinations.contains(panels[target]->currentDir()))
                destinations.append(panels[target]->currentDir());
        }
        QString preferred = destinations.value(0);
        for (const QString &dir : destinations)
            if (!transferDirectoriesMatch(dir, panel->currentDir()))
            {
                preferred = dir;
                break;
            }
        panel->setProperty("otherPaneDirectory", preferred);
        panel->setProperty("otherPaneDirectories", count == 3 ? destinations : QStringList());
        panel->setProperty("tripleBrowse", count == 3);
    }
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
    if (m_tertiaryThumbnailPanel)
        ThumbnailPanelTransfer::showTransferredFiles(*m_tertiaryThumbnailPanel,
                                                     destinationDirectory, createdPaths);
    updateUndoRedoActions();
}
