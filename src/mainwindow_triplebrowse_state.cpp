#include "application/ExternalOpen.h"
#include "browse_sort_restore.h"
#include "mainwindow_p.h"

#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

namespace
{
QString panelCurrentPath(const ThumbnailPanel *panel)
{
    if (!panel)
        return {};
    const QModelIndex idx = panel->currentIndex();
    if (!idx.isValid())
        return {};
    return panel->pathList().value(idx.row());
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

void MainWindow::mirrorPrimaryIntoTertiary()
{
    if (!m_thumbnailPanel || !m_tertiaryThumbnailPanel)
        return;
    const QString leftDir = QDir::cleanPath(m_thumbnailPanel->currentDir());
    if (leftDir.isEmpty())
        return;
    // A stale third folder from an earlier split or session is replaced.
    if (QDir::cleanPath(m_tertiaryThumbnailPanel->currentDir()) != leftDir)
        m_tertiaryThumbnailPanel->setDirectory(leftDir);
    if (m_tertiaryPathEdit)
        m_tertiaryPathEdit->setText(leftDir);
    m_appState.tertiaryLastDir = leftDir;
    m_tertiaryBrowseHistory.push(leftDir);
    m_pendingTertiaryScrollRestore = false;
    // The left pane keeps the selection; the new third pane starts clean.
    // clearSelection alone leaves parked pending/filter restores that can
    // re-select rows after setDirectory / scheduleFilter completes.
    m_tertiaryThumbnailPanel->clearMirroredSelectionState();
}

void MainWindow::applyTertiarySortFromState()
{
    if (!m_tertiaryThumbnailPanel)
        return;
    if (!m_appState.hasTertiarySort)
    {
        if (m_thumbnailPanel)
        {
            m_tertiaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                              m_thumbnailPanel->sortAscending());
        }
        return;
    }
    const auto mode = restoredBrowseSortMode(m_appState.tertiarySortField, m_tertiarySortCombo);
    m_tertiaryThumbnailPanel->setSort(mode, m_appState.tertiarySortAscending);
    if (m_tertiarySortDirBtn)
    {
        const bool desc = !m_appState.tertiarySortAscending;
        const QSignalBlocker b(m_tertiarySortDirBtn);
        m_tertiarySortDirBtn->setChecked(desc);
        m_tertiarySortDirBtn->setText(desc ? QStringLiteral("↓") : QStringLiteral("↑"));
    }
}

void MainWindow::persistTertiaryBrowseState()
{
    if (!m_tertiaryThumbnailPanel)
        return;
    m_appState.tertiaryLastDir = m_tertiaryThumbnailPanel->currentDir();
    m_appState.tertiaryThumbScroll = m_tertiaryThumbnailPanel->scrollOffset();
    QString lastImage;
    if (m_selection)
        lastImage = m_selection->paneCurrentImage(SelectionModel::PaneSlot::Tertiary);
    if (lastImage.isEmpty())
        lastImage = panelCurrentPath(m_tertiaryThumbnailPanel);
    m_appState.tertiaryLastImage = lastImage;
    m_appState.hasTertiarySort = true;
    m_appState.tertiarySortField = static_cast<int>(m_tertiaryThumbnailPanel->sortMode());
    m_appState.tertiarySortAscending = m_tertiaryThumbnailPanel->sortAscending();
    m_appState.tertiaryDirHistory = m_tertiaryBrowseHistory.entries();
    m_appState.tertiaryDirHistoryIndex = m_tertiaryBrowseHistory.index();
}

void MainWindow::restoreTertiaryBrowsePosition()
{
    if (!m_tripleBrowseEnabled || !m_tertiaryThumbnailPanel)
        return;
    if (m_appState.tertiaryThumbScroll <= 0 && m_appState.tertiaryLastImage.isEmpty())
        return;
    m_pendingTertiaryScrollRestore = true;
    if (!m_tertiaryThumbnailPanel->pathList().isEmpty())
        applyPendingTertiaryBrowseRestore(m_tertiaryThumbnailPanel->currentDir(),
                                          m_tertiaryThumbnailPanel->pathList());
}

void MainWindow::applyPendingTertiaryBrowseRestore(const QString &directory,
                                                   const QStringList &paths)
{
    if (!m_pendingTertiaryScrollRestore || paths.isEmpty() || !m_tertiaryThumbnailPanel)
        return;
    const QString want = QDir::cleanPath(m_appState.tertiaryLastDir);
    if (!want.isEmpty())
    {
        const QString have = QDir::cleanPath(directory);
        if (have != want && QDir(directory) != QDir(m_appState.tertiaryLastDir))
            return;
    }
    m_pendingTertiaryScrollRestore = false;
    ThumbnailPanel *panel = m_tertiaryThumbnailPanel;
    const QString lastImage = m_appState.tertiaryLastImage;
    if (!lastImage.isEmpty())
    {
        panel->scrollToPath(lastImage);
        panel->selectPath(lastImage);
    }
    restorePanelScroll(panel, m_appState.tertiaryThumbScroll);
}

void MainWindow::handleTertiaryDroppedPaths(const QStringList &paths)
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
        openThirdDirectory(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Image:
    {
        const QString dir = QFileInfo(plan.paths.front()).absolutePath();
        if (!dir.isEmpty())
            openThirdDirectory(dir);
        if (plan.isCompare())
            openCompare(plan.paths);
        else
            onImageOpenFromPanel(plan.paths.front(), m_tertiaryThumbnailPanel);
        break;
    }
    default:
        handleDroppedPaths(paths);
        break;
    }
}

void MainWindow::wireTertiaryBrowseHostSignals()
{
    if (!m_tertiaryThumbnailPanel)
        return;

    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpenFromPanel(path, m_tertiaryThumbnailPanel); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::currentThumbnailReady, this,
            [this](const QString &path) { onPanelThumbnailReady(m_tertiaryThumbnailPanel, path); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::compareRequested, this,
            [this](const QStringList &images) { openCompare(images); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::usmCompareRequested, this,
            [this](const QString &path) { openUsmCompare(path); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::yuvToolRequested, this,
            &MainWindow::onYuvToolRequested);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::filesDropped, this,
            &MainWindow::handleTertiaryDroppedPaths);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::pathsRemoved, this,
            [this](const QStringList &deleted)
            { onGalleryPathsRemoved(m_tertiaryThumbnailPanel, deleted); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::pathsRenamed, this,
            &MainWindow::onGalleryPathsRenamed);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::pathsModified, this,
            &MainWindow::onGalleryPathsModified);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::statsChanged, this,
            &MainWindow::onTertiaryStatsChanged);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::sequenceChanged, this,
            &MainWindow::onTertiarySequenceChanged);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::browseStatusChanged, this,
            [this](const QString &message)
            {
                if (message.isEmpty() || !statusBar() ||
                    activeThumbnailPanel() != m_tertiaryThumbnailPanel)
                    return;
                statusBar()->showMessage(message, 4000);
            });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::directorySourceChanged, this,
            [this](const QString &) { syncOtherPaneDirectories(); });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::filesTransferred, this,
            &MainWindow::onFilesTransferred);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::sortChanged, this,
            [this](ThumbnailPanel::SortMode mode, bool ascending)
            {
                m_appState.hasTertiarySort = true;
                m_appState.tertiarySortField = static_cast<int>(mode);
                m_appState.tertiarySortAscending = ascending;
            });
}

void MainWindow::onTertiaryStatsChanged(int, qint64, int, qint64)
{
    if (!m_tripleBrowseEnabled && activeThumbnailPanel() != m_tertiaryThumbnailPanel)
        return;
    applyActivePaneStatusBar();
}

void MainWindow::onTertiarySequenceChanged(const QString &directory, const QStringList &paths)
{
    const QString current = currentImagePath();
    if (m_imageList && !current.isEmpty() && paths.contains(current))
        m_imageList->setPaths(paths, directory);
    if (activeThumbnailPanel() == m_tertiaryThumbnailPanel && statusBar() && !paths.isEmpty())
        statusBar()->showMessage(tr("Loaded %1 images").arg(paths.size()), 2500);
    applyPendingTertiaryBrowseRestore(directory, paths);
}
