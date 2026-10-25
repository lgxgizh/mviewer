// MainWindow selection helpers: the SelectionModel <-> gallery sync and the
// selection-driven action enablement. Split from mainwindow.cpp to keep the
// ADR-014 1000-line cap headroom; behavior is unchanged.
#include "mainwindow_p.h"

#include "ShortcutHelper.h"

namespace
{
void enableActionIf(QAction *act, bool on)
{
    if (act)
        act->setEnabled(on);
}

void updateCompareAction(QAction *act, int selectedCount)
{
    if (!act)
        return;
    const bool canCompare = (selectedCount >= 2 && selectedCount <= 8);
    act->setEnabled(canCompare);
    act->setToolTip(mviewer::ui::appendShortcutHint(
        canCompare ? MainWindow::tr("Compare") : MainWindow::tr("Select 2–8 images to compare"),
        mviewer::ui::actionShortcutHint(act)));
}
} // namespace

QStringList MainWindow::resolveSelectedPaths(bool preferMulti) const
{
    // A-3: single source of truth — SelectionModel first, then gallery.
    if (m_selection)
    {
        const QStringList sel = m_selection->selection();
        if (preferMulti && sel.size() >= 1)
            return sel;
        if (!preferMulti && !m_selection->currentImage().isEmpty())
            return {m_selection->currentImage()};
        if (!sel.isEmpty())
            return sel;
    }
    if (m_thumbnailPanel)
    {
        const QStringList gallery = m_thumbnailPanel->selectedPaths();
        if (!gallery.isEmpty())
            return gallery;
    }
    return {};
}

void MainWindow::updateSelectionActions()
{
    const int n = m_selection ? m_selection->selection().size() : 0;
    const int availableCount =
        std::max(m_imageList ? m_imageList->count() : 0,
                 m_thumbnailPanel ? static_cast<int>(m_thumbnailPanel->pathList().size()) : 0);
    const bool hasImages = availableCount > 0;
    const bool hasCurrent = !currentImagePath().isEmpty();
    const bool viewerVisible = m_imageViewer && m_imageViewer->isVisible();
    updateCompareAction(m_actCompare, n);
    enableActionIf(m_actExportImages, n >= 1 || hasImages);
    enableActionIf(m_actBatch, n >= 1 || hasImages);
    if (m_actToggleMetadata)
    {
        m_actToggleMetadata->setEnabled(hasCurrent);
        if (!hasCurrent)
            m_actToggleMetadata->setChecked(false);
    }
    const bool canZoom = hasCurrent && viewerVisible;
    for (QAction *act : {m_actZoomIn, m_actZoomOut, m_actZoomFit, m_actZoomActual})
        enableActionIf(act, canZoom);
    if (m_zoomPresetsMenu)
        m_zoomPresetsMenu->menuAction()->setEnabled(canZoom);
    enableActionIf(m_actSlideshow, hasCurrent && !currentDir().isEmpty());
    const bool canTransform = hasCurrent || (n > 0);
    for (QAction *act : {m_actRotateCW, m_actRotateCCW, m_actFlipH, m_actFlipV})
        enableActionIf(act, canTransform);
    updateOtherPaneTransferActions();
    updateNavigationActions();
}

QString MainWindow::currentDir() const
{
    return m_directory ? m_directory->currentDirectory() : QString();
}

QString MainWindow::currentImagePath() const
{
    return m_selection ? m_selection->currentImage() : QString();
}

void MainWindow::ensureImageList()
{
    if (!m_imageList || !m_directory)
        return;
    const QString dir = m_directory->currentDirectory();
    if (dir.isEmpty())
        return;
    // M37: ThumbnailPanel owns directory scan/sort/filter; do not enumerate here.
    if (m_imageList->directory() != dir)
        m_imageList->setPaths({}, dir);
}

static void syncPanelSlot(ThumbnailPanel *panel, const QStringList &sel, const QString &cur)
{
    if (!panel)
        return;
    const QStringList gallery = panel->selectedPaths();
    if (gallery == sel)
    {
        if (!cur.isEmpty())
        {
            const QModelIndex curIdx = panel->currentIndex();
            const QString curPath =
                curIdx.isValid() ? panel->pathList().value(curIdx.row()) : QString();
            if (curPath != cur)
                panel->selectPath(cur);
        }
        return;
    }
    if (sel.size() <= 1)
    {
        if (!cur.isEmpty())
            panel->selectPath(cur);
        else if (!sel.isEmpty())
            panel->selectPath(sel.first());
        else
            panel->selectPaths({}, {}, false);
    }
    else
    {
        panel->selectPaths(sel, cur, false);
    }
}

void MainWindow::syncGalleryFromSelection()
{
    if (!m_selection || m_syncingSelection)
        return;
    m_syncingSelection = true;
    if (!m_splitBrowseEnabled)
    {
        syncPanelSlot(m_thumbnailPanel, m_selection->selection(), m_selection->currentImage());
    }
    else
    {
        syncPanelSlot(m_thumbnailPanel,
                      m_selection->paneSelection(SelectionModel::PaneSlot::Primary),
                      m_selection->paneCurrentImage(SelectionModel::PaneSlot::Primary));
        if (m_secondaryThumbnailPanel && m_secondaryThumbnailPanel->isVisible())
        {
            syncPanelSlot(m_secondaryThumbnailPanel,
                          m_selection->paneSelection(SelectionModel::PaneSlot::Secondary),
                          m_selection->paneCurrentImage(SelectionModel::PaneSlot::Secondary));
        }
    }
    m_syncingSelection = false;
}
