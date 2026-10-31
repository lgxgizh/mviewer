// Browse navigation and gallery signal wiring, split out of
// mainwindow_ui_connections.cpp to stay under the line budget.
#include "mainwindow_p.h"

#include <QHash>

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
// Images whose base name matches an added, modified or removed sidecar. The
// gallery is indexed once per delta so a sidecar burst is not files x sidecars.
QStringList imagesForSidecarDelta(const mviewer::core::DirectoryDelta &delta,
                                  const QList<ThumbnailPanel::Entry> &images)
{
    const std::vector<mviewer::core::DirectoryEntry> *lists[] = {
        &delta.sidecarAdded, &delta.sidecarModified, &delta.sidecarRemoved};
    QStringList out;
    if (std::all_of(std::begin(lists), std::end(lists), [](const auto *l) { return l->empty(); }))
        return out;
    QHash<QString, QStringList> imagesByBase;
    imagesByBase.reserve(images.size());
    for (const auto &image : images)
        imagesByBase[QFileInfo(image.path).completeBaseName()].append(image.path);
    for (const auto *list : lists)
    {
        for (const auto &entry : *list)
        {
            const QString sidecar =
                QString::fromUtf8(entry.path.data(), static_cast<int>(entry.path.size()));
            const auto found = imagesByBase.constFind(QFileInfo(sidecar).completeBaseName());
            if (found != imagesByBase.cend())
                out.append(found.value());
        }
    }
    return out;
}
} // namespace

void MainWindow::connectNavigationSignals()
{
    m_thumbnailPanel->setLiveDirectoryMonitoring(true);
    connect(m_thumbnailPanel, &ThumbnailPanel::directorySourceChanged, m_directoryMonitor,
            &DirectoryMonitor::setActiveDirectory);
    connect(m_directoryTree, &DirectoryTree::directoryChanged, m_thumbnailPanel,
            &ThumbnailPanel::setDirectory);
    connect(m_directoryTree, &DirectoryTree::openInSecondaryPaneRequested, this,
            &MainWindow::openRightDirectory);
    connect(m_directoryTree, &DirectoryTree::directoryContentsChanged, m_directoryMonitor,
            &DirectoryMonitor::notifyDirectoryChanged);
    connect(m_thumbnailPanel, &ThumbnailPanel::directoryContentsChanged, m_directoryMonitor,
            &DirectoryMonitor::notifyDirectoryChanged);
    connect(m_directoryMonitor, &DirectoryMonitor::directoryDeltaReady, m_thumbnailPanel,
            &ThumbnailPanel::applyDirectoryDelta);
    connect(m_directoryMonitor, &DirectoryMonitor::directoryDeltaReady, this,
            [this](const mviewer::core::DirectoryDelta &delta)
            {
                QStringList sidecarImages =
                    imagesForSidecarDelta(delta, m_thumbnailPanel->entries());
                sidecarImages.removeDuplicates();
                scheduleSidecarImportPaths(sidecarImages);
            });
    connect(m_directoryMonitor, &DirectoryMonitor::directoryAvailabilityChanged, this,
            [this](const QString &path, bool available)
            {
                if (!m_directory ||
                    !DirectoryTree::equivalentPath(m_directory->currentDirectory(), path))
                    return;
                if (!available)
                    statusBar()->showMessage(tr("Directory temporarily unavailable: %1").arg(path));
                else if (statusBar()->currentMessage().startsWith(
                             tr("Directory temporarily unavailable: %1").section('%', 0, 0)))
                    statusBar()->clearMessage();
            });
    // M37: the asynchronous gallery scan publishes the final visible sequence
    // into the single navigation model. Viewer, keyboard navigation, Compare
    // and preload all consume this same ordered list.
    connect(m_thumbnailPanel, &ThumbnailPanel::sequenceChanged, this,
            [this](const QString &directory, const QStringList &paths)
            {
                if (!m_imageList)
                    return;
                const QString current = currentImagePath();
                // In split mode keep the viewer sequence on the right pane only
                // while the current image actually comes from that pane.
                const bool currentFromSecondary =
                    m_splitBrowseEnabled && m_secondaryThumbnailPanel && !paths.contains(current) &&
                    m_secondaryThumbnailPanel->rowForPath(current) >= 0;
                const bool viewingThis = current.isEmpty() || !currentFromSecondary;
                if (viewingThis)
                    m_imageList->setPaths(paths, directory);
                if (activeThumbnailPanel() == m_thumbnailPanel && !paths.isEmpty() && m_directory &&
                    DirectoryTree::equivalentPath(m_directory->currentDirectory(), directory))
                    statusBar()->showMessage(tr("Loaded %1 images").arg(paths.size()), 2500);
            });
    connect(m_imageList, &ImageListModel::pathsChanged, m_imageViewer,
            &ImageViewer::setBrowseSequence);
    connect(m_breadcrumb, &BreadcrumbBar::pathSelected, this, &MainWindow::onBreadcrumbPath);
    connect(m_directoryTree, &DirectoryTree::directoryChanged, this,
            [this](const QString &path)
            {
                // The old gallery can still be visible while the new folder
                // scans on a worker thread. Clear the SSOT first so no stale
                // preview, status identity, or quick-preview target leaks
                // across folders. The stats callback below selects the first
                // new item exactly once after the scan has produced rows.
                m_autoSelectFirstPending = true;
                clearPrimarySelectionForFolderChange();
                resetFiltersOnFolderChange();
                refreshBrowserLocation(path);
                m_directory->setCurrentDirectory(path);
                if (m_directoryMonitor)
                    m_directoryMonitor->setActiveDirectory(path);
                m_workspace->setRootPath(path);
                // ThumbnailPanel clears and later publishes the final sequence.
                // Do not enumerate the directory synchronously here.
                // P0-1: record this folder in the recent-folders LRU + repopulate
                // the Recent menu.
                m_recent.add(path.toStdString());
                m_appState.addRecentFolder(path);
                m_directory->addRecentFolder(path);
                rebuildRecentMenu();
                // P0: the committed transition is the only history owner.
                pushDirHistory(path);
                // M50: the same committed transition owns cancellable Sidecar
                // import for every navigation entry point, including tree
                // clicks/Enter, breadcrumb, Back/Forward, refresh, and restore.
                scheduleSidecarImport(path);
                statusBar()->showMessage(tr("Scanning…"), 8000);
                // With no image selected yet, the title carries the folder.
                if (currentImagePath().isEmpty())
                    setWindowTitle(QString("%1 - MViewer").arg(QDir(path).dirName()));
                scheduleReindex();
                if (m_emptyFolderTimer)
                    m_emptyFolderTimer->stop();
                if (m_emptyFolderLabel)
                    m_emptyFolderLabel->hide();
                updateEmptyState();
                updateSelectionActions();
            });
}
void MainWindow::connectGallerySignals()
{
    // Tree highlight waits for this so the first gallery batch is not queued
    // behind QFileSystemModel::index.
    connect(m_thumbnailPanel, &ThumbnailPanel::galleryModelReady, m_directoryTree,
            &DirectoryTree::nudgeDeferredHighlight);
    connect(m_thumbnailPanel, &ThumbnailPanel::browseStatusChanged, this,
            [this](const QString &message)
            {
                if (message.isEmpty() || !statusBar())
                    return;
                if (activeThumbnailPanel() != m_thumbnailPanel)
                    return;
                statusBar()->showMessage(message, 4000);
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::statsChanged, this,
            [this](int total, qint64, int, qint64)
            {
                // Empty-folder feedback: once the gallery settles with
                // zero displayable entries, defer showing the hint past the
                // transient pre-scan zero; any real content cancels it.
                // Files hidden only by the type filter get the panel's own
                // "N files hidden" hint instead of the generic empty card.
                if (total > 0 || m_thumbnailPanel->typeHiddenCount() > 0)
                {
                    if (m_emptyFolderTimer)
                        m_emptyFolderTimer->stop();
                    if (m_emptyFolderLabel && m_emptyFolderLabel->isVisible())
                        m_emptyFolderLabel->hide();
                }
                else if (m_emptyFolderTimer && m_emptyFolderLabel &&
                         !m_emptyFolderLabel->isVisible())
                {
                    m_emptyFolderTimer->start();
                }
                if (!m_autoSelectFirstPending || total <= 0 || !m_selection || !m_thumbnailPanel)
                    return;
                // In split mode the merged current image can be the right pane.
                // Only a left-pane current image cancels this auto-select.
                const bool split = m_selection->isSplitModeEnabled();
                const QString already =
                    split ? m_selection->paneCurrentImage(SelectionModel::PaneSlot::Primary)
                          : m_selection->currentImage();
                if (!already.isEmpty())
                {
                    m_autoSelectFirstPending = false;
                    return;
                }
                const QString first = m_thumbnailPanel->pathList().value(0);
                if (first.isEmpty())
                    return;
                m_autoSelectFirstPending = false;
                if (split)
                {
                    m_selection->setPaneSelection(SelectionModel::PaneSlot::Primary,
                                                  QStringList{first}, first);
                    syncGalleryFromSelection();
                }
                else
                    m_selection->setCurrentImage(first);
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::currentThumbnailReady, this,
            [this](const QString &path) { onPanelThumbnailReady(m_thumbnailPanel, path); });
}

void MainWindow::onPanelThumbnailReady(ThumbnailPanel *panel, const QString &path)
{
    if (!panel || !m_previewPanel || !m_selection || m_selection->currentImage() != path)
        return;
    const QPixmap warm = panel->thumbReady(path);
    if (warm.isNull())
        return;
    qint64 knownFileSize = -1;
    const QSize knownSourceSize = knownSourceSizeFor(panel, path, &knownFileSize);
    m_previewPanel->offerWarmThumbnail(path, warm, knownSourceSize, knownFileSize);
    // The viewer only takes it for the path it is loading; setProvisionalImage
    // itself ignores a thumbnail once the sharp image is on screen.
    if (m_imageViewer && !m_imageViewer->isHidden() &&
        ImageViewer::browsePathEquals(m_imageViewer->currentPath(), path))
        m_imageViewer->setProvisionalImage(path, warm.toImage(), knownSourceSize);
}

void MainWindow::clearPrimarySelectionForFolderChange()
{
    if (!m_selection)
        return;
    // The right pane keeps its own selection. Single-pane still drops focused
    // and compared images with the selection.
    if (m_selection->isSplitModeEnabled())
        m_selection->clearPane(SelectionModel::PaneSlot::Primary);
    else
        m_selection->clear();
}
