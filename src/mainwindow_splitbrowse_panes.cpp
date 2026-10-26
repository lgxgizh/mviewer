#include "mainwindow.h"
#include "mainwindow_p.h"
#include "splitbrowse_statusbar.h"

#include "analysispanel.h"
#include "application/ExternalOpen.h"
#include "core/filesystem/DirectorySnapshot.h"
#include "fileop_targets.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include "analyzermodel.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QWidget>
#include <utility>

namespace
{
bool panelContainsPath(const ThumbnailPanel *panel, const QString &path)
{
    return panel && !path.isEmpty() && panel->rowForPath(path) >= 0;
}

QString panelCurrentPath(const ThumbnailPanel *panel)
{
    if (!panel)
        return {};
    const QModelIndex idx = panel->currentIndex();
    if (!idx.isValid())
        return {};
    return panel->pathList().value(idx.row());
}

void applyPaneActiveStyle(QWidget *pane, bool active)
{
    if (!pane)
        return;
    pane->setProperty("activePane", active);
    if (QStyle *style = pane->style())
    {
        style->unpolish(pane);
        style->polish(pane);
    }
    pane->update();
}

// isVisible() is false when any ancestor (including an unshown top-level) is
// hidden. isVisibleTo(host) only requires this widget's own hide()/show() chain
// up to host, so pane routing works in headless tests that never show().
bool browsePaneExposed(const QWidget *pane, const QWidget *host)
{
    return pane && host && pane->isVisibleTo(host);
}

void restorePanelScroll(ThumbnailPanel *panel, int scroll)
{
    if (!panel || scroll <= 0)
        return;
    QScrollBar *bar = panel->verticalScrollBar();
    if (!bar)
        return;
    bar->setValue(scroll);
    if (bar->maximum() >= scroll && bar->value() == scroll)
        return;
    auto *timer = new QTimer(panel);
    timer->setInterval(0);
    QObject::connect(timer, &QTimer::timeout, panel,
                     [bar, scroll, timer, hops = 0]() mutable
                     {
                         if (bar->maximum() > 0)
                         {
                             bar->setValue(qMin(scroll, bar->maximum()));
                             timer->stop();
                             timer->deleteLater();
                             return;
                         }
                         if (++hops >= 40)
                         {
                             timer->stop();
                             timer->deleteLater();
                         }
                     });
    timer->start();
}
} // namespace

ThumbnailPanel *MainWindow::activeThumbnailPanel() const
{
    if (m_splitBrowseEnabled && m_secondaryThumbnailPanel &&
        browsePaneExposed(m_secondaryBrowsePane, this) &&
        m_activeBrowsePane == SelectionModel::PaneSlot::Secondary)
        return m_secondaryThumbnailPanel;
    return m_thumbnailPanel;
}

ThumbnailPanel *MainWindow::thumbnailPanelForPath(const QString &path, ThumbnailPanel *prefer) const
{
    if (panelContainsPath(prefer, path))
        return prefer;
    if (m_splitBrowseEnabled && panelContainsPath(m_secondaryThumbnailPanel, path))
        return m_secondaryThumbnailPanel;
    if (panelContainsPath(m_thumbnailPanel, path))
        return m_thumbnailPanel;
    if (prefer)
        return prefer;
    return activeThumbnailPanel();
}

void MainWindow::applyGallerySplitterSizes()
{
    if (!m_gallerySplitter)
        return;
    if (m_appState.gallerySplitterSizes.size() == 2 && m_appState.gallerySplitterSizes[0] > 0 &&
        m_appState.gallerySplitterSizes[1] > 0)
    {
        m_gallerySplitter->setSizes(m_appState.gallerySplitterSizes);
        return;
    }
    const int w = m_gallerySplitter->width();
    const int half = w > 0 ? w / 2 : 400;
    m_gallerySplitter->setSizes({half, half});
}

void MainWindow::updateActivePaneIndicator()
{
    const bool split = m_splitBrowseEnabled && browsePaneExposed(m_secondaryBrowsePane, this);
    const bool secondaryActive = split && m_activeBrowsePane == SelectionModel::PaneSlot::Secondary;
    applyPaneActiveStyle(m_primaryBrowsePane, split && !secondaryActive);
    applyPaneActiveStyle(m_secondaryBrowsePane, secondaryActive);
}

void MainWindow::updateSwapButtonEnabled()
{
    if (!m_secondarySwapBtn)
        return;
    const bool leftOpen = m_thumbnailPanel && !m_thumbnailPanel->currentDir().isEmpty();
    const bool rightOpen =
        m_secondaryThumbnailPanel && !m_secondaryThumbnailPanel->currentDir().isEmpty();
    m_secondarySwapBtn->setEnabled(m_splitBrowseEnabled && leftOpen && rightOpen);
}

void MainWindow::enableSplitBrowseUi()
{
    // Interactive split: one pane becomes two showing the same contents. The
    // right pane mirrors the left folder, sort, type filter (shared), view mode
    // and thumbnail size every time; afterwards both panes navigate on their
    // own. Session restore instead brings back the saved right pane.
    const bool mirror =
        !m_splitRestoringSession && m_thumbnailPanel && !m_thumbnailPanel->currentDir().isEmpty();
    if (m_thumbnailPanel && m_secondaryThumbnailPanel)
    {
        m_secondaryThumbnailPanel->setThumbSize(m_thumbnailPanel->thumbSize());
        m_secondaryThumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());
        if (mirror)
            m_secondaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                               m_thumbnailPanel->sortAscending());
        else
            applySecondarySortFromState();
    }
    if (m_secondaryBrowsePane)
    {
        m_secondaryBrowsePane->show();
        applyGallerySplitterSizes();
    }
    if (mirror && !m_splitOpeningTarget)
    {
        mirrorPrimaryIntoSecondary();
    }
    else if (!mirror && !m_splitOpeningTarget && m_secondaryThumbnailPanel &&
             m_secondaryThumbnailPanel->currentDir().isEmpty())
    {
        QString targetDir = m_appState.secondaryLastDir;
        if (targetDir.isEmpty() || !QDir(targetDir).exists())
        {
            if (m_thumbnailPanel && !m_thumbnailPanel->currentDir().isEmpty())
                targetDir = m_thumbnailPanel->currentDir();
        }
        if (!targetDir.isEmpty() && QDir(targetDir).exists())
            openRightDirectory(targetDir);
    }
    updateActivePaneIndicator();
    updateSwapButtonEnabled();
    updateSecondaryNavButtons();
    applyActivePaneStatusBar();
}

void MainWindow::mirrorPrimaryIntoSecondary()
{
    if (!m_thumbnailPanel || !m_secondaryThumbnailPanel)
        return;
    const QString leftDir = QDir::cleanPath(m_thumbnailPanel->currentDir());
    if (leftDir.isEmpty())
        return;
    // A stale right folder from an earlier split or session is replaced.
    if (QDir::cleanPath(m_secondaryThumbnailPanel->currentDir()) != leftDir)
        m_secondaryThumbnailPanel->setDirectory(leftDir);
    if (m_secondaryPathEdit)
        m_secondaryPathEdit->setText(leftDir);
    m_appState.secondaryLastDir = leftDir;
    m_secondaryBrowseHistory.push(leftDir);
    m_pendingSecondaryScrollRestore = false;
    // The left pane keeps the selection; the new right pane starts clean.
    m_secondaryThumbnailPanel->clearSelection();
    if (m_selection)
        m_selection->setPaneSelection(SelectionModel::PaneSlot::Secondary, QStringList());
}

void MainWindow::disableSplitBrowseUi()
{
    m_pendingSecondaryScrollRestore = false;
    if (m_secondaryBrowsePane)
        m_secondaryBrowsePane->hide();
    setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    updateActivePaneIndicator();
    updateSwapButtonEnabled();
    updateSecondaryNavButtons();
    applyActivePaneStatusBar();
}

void MainWindow::applySecondarySortFromState()
{
    if (!m_secondaryThumbnailPanel)
        return;
    if (!m_appState.hasSecondarySort)
    {
        if (m_thumbnailPanel)
        {
            m_secondaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                               m_thumbnailPanel->sortAscending());
        }
        return;
    }
    const auto mode = static_cast<ThumbnailPanel::SortMode>(m_appState.secondarySortField);
    m_secondaryThumbnailPanel->setSort(mode, m_appState.secondarySortAscending);
    if (m_secondarySortCombo)
    {
        const int idx = m_secondarySortCombo->findData(mode);
        if (idx >= 0)
        {
            const QSignalBlocker b(m_secondarySortCombo);
            m_secondarySortCombo->setCurrentIndex(idx);
        }
    }
    if (m_secondarySortDirBtn)
    {
        const bool desc = !m_appState.secondarySortAscending;
        const QSignalBlocker b(m_secondarySortDirBtn);
        m_secondarySortDirBtn->setChecked(desc);
        m_secondarySortDirBtn->setText(desc ? QStringLiteral("↓") : QStringLiteral("↑"));
    }
}

void MainWindow::persistSecondaryBrowseState()
{
    if (!m_secondaryThumbnailPanel)
        return;
    m_appState.secondaryLastDir = m_secondaryThumbnailPanel->currentDir();
    m_appState.secondaryThumbScroll = m_secondaryThumbnailPanel->scrollOffset();
    QString lastImage;
    if (m_selection)
        lastImage = m_selection->paneCurrentImage(SelectionModel::PaneSlot::Secondary);
    if (lastImage.isEmpty())
        lastImage = panelCurrentPath(m_secondaryThumbnailPanel);
    m_appState.secondaryLastImage = lastImage;
    m_appState.hasSecondarySort = true;
    m_appState.secondarySortField = static_cast<int>(m_secondaryThumbnailPanel->sortMode());
    m_appState.secondarySortAscending = m_secondaryThumbnailPanel->sortAscending();
    m_appState.secondaryDirHistory = m_secondaryBrowseHistory.entries();
    m_appState.secondaryDirHistoryIndex = m_secondaryBrowseHistory.index();
}

void MainWindow::restoreSecondaryBrowsePosition()
{
    if (!m_splitBrowseEnabled || !m_secondaryThumbnailPanel)
        return;
    if (m_appState.secondaryThumbScroll <= 0 && m_appState.secondaryLastImage.isEmpty())
        return;
    m_pendingSecondaryScrollRestore = true;
    if (!m_secondaryThumbnailPanel->pathList().isEmpty())
        applyPendingSecondaryBrowseRestore(m_secondaryThumbnailPanel->currentDir(),
                                           m_secondaryThumbnailPanel->pathList());
}

void MainWindow::applyPendingSecondaryBrowseRestore(const QString &directory,
                                                    const QStringList &paths)
{
    if (!m_pendingSecondaryScrollRestore || paths.isEmpty() || !m_secondaryThumbnailPanel)
        return;
    const QString want = QDir::cleanPath(m_appState.secondaryLastDir);
    if (!want.isEmpty())
    {
        const QString have = QDir::cleanPath(directory);
        if (have != want && QDir(directory) != QDir(m_appState.secondaryLastDir))
            return;
    }
    m_pendingSecondaryScrollRestore = false;
    ThumbnailPanel *panel = m_secondaryThumbnailPanel;
    const QString lastImage = m_appState.secondaryLastImage;
    if (!lastImage.isEmpty())
    {
        panel->scrollToPath(lastImage);
        panel->selectPath(lastImage);
    }
    restorePanelScroll(panel, m_appState.secondaryThumbScroll);
}

void MainWindow::refreshVisibleBrowsePanes()
{
    if (m_thumbnailPanel && browsePaneExposed(m_thumbnailPanel, this) &&
        !m_thumbnailPanel->currentDir().isEmpty())
        m_thumbnailPanel->refresh();
    if (m_splitBrowseEnabled && m_secondaryThumbnailPanel &&
        browsePaneExposed(m_secondaryBrowsePane, this) &&
        !m_secondaryThumbnailPanel->currentDir().isEmpty())
        m_secondaryThumbnailPanel->refresh();
}

void MainWindow::dropRemovedPathsFromPanel(ThumbnailPanel *panel, const QStringList &deleted)
{
    if (!panel || panel->currentDir().isEmpty() || deleted.isEmpty())
        return;
    mviewer::core::DirectoryDelta delta;
    delta.path = panel->currentDir().toUtf8().toStdString();
    for (const QString &path : deleted)
    {
        if (panel->rowForPath(path) < 0)
            continue;
        mviewer::core::DirectoryEntry entry;
        entry.path = path.toUtf8().toStdString();
        delta.removed.push_back(std::move(entry));
    }
    if (!delta.removed.empty())
        panel->applyDirectoryDelta(delta);
}

void MainWindow::onGalleryPathsRemoved(ThumbnailPanel *source, const QStringList &deleted)
{
    const QString shown = currentImagePath();
    const bool viewerOpen = m_imageViewer && !m_imageViewer->isHidden();
    int oldIndex = -1;
    int oldCount = 0;
    QList<int> removedIndices;
    if (viewerOpen && m_imageList && !shown.isEmpty())
    {
        const QStringList before = m_imageList->paths();
        oldCount = static_cast<int>(before.size());
        oldIndex = static_cast<int>(before.indexOf(shown));
        for (const QString &path : deleted)
        {
            const int index = static_cast<int>(before.indexOf(path));
            if (index >= 0)
                removedIndices.append(index);
        }
    }
    if (m_imageList)
        m_imageList->removePaths(deleted);
    if (m_thumbnailPanel && m_thumbnailPanel != source)
        dropRemovedPathsFromPanel(m_thumbnailPanel, deleted);
    if (m_secondaryThumbnailPanel && m_secondaryThumbnailPanel != source)
        dropRemovedPathsFromPanel(m_secondaryThumbnailPanel, deleted);
    if (!viewerOpen || shown.isEmpty() || !deleted.contains(shown))
        return;
    const int next = nextIndexAfterRemoval(oldIndex, oldCount, removedIndices);
    if (next < 0 || !m_imageList || next >= m_imageList->paths().size())
    {
        m_imageViewer->close();
        return;
    }
    if (m_selection)
        m_selection->setCurrentImage(m_imageList->paths().at(next));
}

void MainWindow::onGalleryPathsRenamed(const QStringList &oldPaths, const QStringList &newPaths)
{
    const QString before = currentImagePath();
    if (m_imageViewer)
        m_imageViewer->renameBrowsePaths(oldPaths, newPaths);
    const int index = oldPaths.indexOf(before);
    if (index < 0 || index >= newPaths.size())
        return;
    const QString after = newPaths.at(index);
    if (m_previewPanel)
        m_previewPanel->setImage(after);
    if (m_metadataPanel)
        m_metadataPanel->setImage(after);
}

void MainWindow::onGalleryPathsModified(const QStringList &paths)
{
    for (const QString &path : paths)
    {
        if (m_imageViewer)
            m_imageViewer->refreshSource(path);
        if (m_thumbnailPanel)
            m_thumbnailPanel->invalidateSourceImage(path);
        if (m_secondaryThumbnailPanel)
            m_secondaryThumbnailPanel->invalidateSourceImage(path);
        if (path == currentImagePath())
        {
            if (m_previewPanel)
                m_previewPanel->setImage(path);
            if (m_metadataPanel)
                m_metadataPanel->setImage(path);
        }
    }
}

void MainWindow::handleSecondaryDroppedPaths(const QStringList &paths)
{
    const auto plan = mviewer::application::planExternalOpen(paths);
    if (!plan.isValid())
    {
        handleDroppedPaths(paths);
        return;
    }
    switch (plan.kind)
    {
    case mviewer::application::ExternalOpenKind::Directory:
        openRightDirectory(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Image:
    {
        const QString dir = QFileInfo(plan.paths.front()).absolutePath();
        if (!dir.isEmpty())
            openRightDirectory(dir);
        if (plan.isCompare())
            openCompare(plan.paths);
        else
            onImageOpenFromPanel(plan.paths.front(), m_secondaryThumbnailPanel);
        break;
    }
    default:
        handleDroppedPaths(paths);
        break;
    }
}

void MainWindow::applyActivePaneStatusBar()
{
    ThumbnailPanel *panel = activeThumbnailPanel();
    if (!panel || !m_lblCount)
        return;
    const QString current = currentImagePath();
    const int total = panel->pathList().size();
    const int idx = activePaneImageIndex(panel->pathList(), current);
    if (total > 0 && idx >= 0)
        m_lblCount->setText(tr("Image %1 / %2").arg(idx + 1).arg(total));
    else
        m_lblCount->setText(tr("%1 images").arg(total));
    if (!m_lblSize)
        return;
    if (m_splitBrowseEnabled)
    {
        const int left = m_thumbnailPanel ? m_thumbnailPanel->selectedPaths().size() : 0;
        const int right =
            m_secondaryThumbnailPanel ? m_secondaryThumbnailPanel->selectedPaths().size() : 0;
        m_lblSize->setText(splitSelectionStatusText(left, right));
        m_lblSize->show();
        return;
    }
    m_lblSize->hide();
}

void MainWindow::onImageOpenFromPanel(const QString &path, ThumbnailPanel *source)
{
    ThumbnailPanel *panel = thumbnailPanelForPath(path, source);
    const bool wasHidden = m_imageViewer->isHidden();
    m_selection->setCurrentImage(path);
    if (wasHidden)
    {
        QStringList sequence = panel ? panel->pathList() : QStringList();
        if (!panel || panel->rowForPath(path) < 0)
        {
            if (m_imageList && m_imageList->indexOf(path) >= 0)
                sequence = m_imageList->paths();
            else
                sequence = {path};
        }
        m_imageViewer->setBrowseSequence(sequence);
        if (m_imageList && panel)
            m_imageList->setPaths(sequence, panel->currentDir());
        QScreen *targetScreen = screen();
        if (!targetScreen)
            targetScreen = QGuiApplication::primaryScreen();
        m_imageViewer->prepareBrowseFullscreen(targetScreen);

        QPixmap warm;
        if (panel)
            warm = panel->thumbReady(path);
        if (warm.isNull() && m_previewPanel && m_previewPanel->presentedPath() == path)
            warm = m_previewPanel->currentPixmap();

        QSize knownSize;
        if (panel)
        {
            if (const auto *entry = panel->entryForPath(path))
                knownSize = QSize(entry->width, entry->height);
        }
        if (!knownSize.isValid() && m_previewPanel && m_previewPanel->presentedPath() == path)
            knownSize = m_previewPanel->sourceImageSize();

        if (!warm.isNull())
            m_imageViewer->setProvisionalImage(path, warm.toImage(), knownSize);

        m_imageViewer->setImage(path);
    }

    pushHistory(path);
    m_appState.addHistory(path);
    m_recentFiles.add(path.toUtf8().toStdString());
    rebuildRecentFilesMenu();
    const QString savedAnalysis = m_analyzer->resultText(path);
    if (!savedAnalysis.isEmpty())
        m_analysisPanel->setRegionStats(savedAnalysis);
    if (wasHidden)
        m_imageViewer->showBrowseFullscreen();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
    updateSelectionActions();
}

void MainWindow::wireSecondaryBrowseHostSignals()
{
    if (!m_secondaryThumbnailPanel)
        return;

    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpenFromPanel(path, m_secondaryThumbnailPanel); });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::compareRequested, this,
            [this](const QStringList &images) { openCompare(images); });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::usmCompareRequested, this,
            [this](const QString &path) { openUsmCompare(path); });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::filesDropped, this,
            &MainWindow::handleSecondaryDroppedPaths);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::pathsRemoved, this,
            [this](const QStringList &deleted)
            { onGalleryPathsRemoved(m_secondaryThumbnailPanel, deleted); });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::pathsRenamed, this,
            &MainWindow::onGalleryPathsRenamed);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::pathsModified, this,
            &MainWindow::onGalleryPathsModified);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::statsChanged, this,
            &MainWindow::onSecondaryStatsChanged);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::sequenceChanged, this,
            &MainWindow::onSecondarySequenceChanged);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::browseStatusChanged, this,
            [this](const QString &message)
            {
                if (message.isEmpty() || !statusBar() ||
                    activeThumbnailPanel() != m_secondaryThumbnailPanel)
                    return;
                statusBar()->showMessage(message, 4000);
            });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::directorySourceChanged, this,
            [this](const QString &) { syncOtherPaneDirectories(); });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::filesTransferred, this,
            &MainWindow::onFilesTransferred);
    if (m_thumbnailPanel)
    {
        connect(m_thumbnailPanel, &ThumbnailPanel::directorySourceChanged, this,
                [this](const QString &) { syncOtherPaneDirectories(); });
        connect(m_thumbnailPanel, &ThumbnailPanel::filesTransferred, this,
                &MainWindow::onFilesTransferred);
    }
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::sortChanged, this,
            [this](ThumbnailPanel::SortMode mode, bool ascending)
            {
                m_appState.hasSecondarySort = true;
                m_appState.secondarySortField = static_cast<int>(mode);
                m_appState.secondarySortAscending = ascending;
            });

    if (m_selection)
    {
        connect(m_selection, &SelectionModel::activePaneChanged, this,
                [this](SelectionModel::PaneSlot)
                {
                    updateActivePaneIndicator();
                    applyActivePaneStatusBar();
                    // Copy/move to the other pane depend on the active pane's
                    // selection and directory, so re-evaluate them here too.
                    updateOtherPaneTransferActions();
                });
    }

    connect(qApp, &QApplication::focusChanged, this, &MainWindow::onBrowseFocusChanged);
}

void MainWindow::onSecondaryStatsChanged(int, qint64, int, qint64)
{
    if (!m_splitBrowseEnabled && activeThumbnailPanel() != m_secondaryThumbnailPanel)
        return;
    applyActivePaneStatusBar();
}

void MainWindow::onSecondarySequenceChanged(const QString &directory, const QStringList &paths)
{
    const QString current = currentImagePath();
    if (m_imageList && !current.isEmpty() && paths.contains(current))
        m_imageList->setPaths(paths, directory);
    if (activeThumbnailPanel() == m_secondaryThumbnailPanel && statusBar() && !paths.isEmpty())
        statusBar()->showMessage(tr("Loaded %1 images").arg(paths.size()), 2500);
    applyPendingSecondaryBrowseRestore(directory, paths);
}

void MainWindow::onBrowseFocusChanged(QWidget *, QWidget *now)
{
    if (!now || !m_splitBrowseEnabled)
        return;
    if (m_secondaryBrowsePane &&
        (now == m_secondaryBrowsePane || m_secondaryBrowsePane->isAncestorOf(now)))
        setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    else if (m_primaryBrowsePane &&
             (now == m_primaryBrowsePane || m_primaryBrowsePane->isAncestorOf(now)))
        setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
}
