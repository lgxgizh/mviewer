// MainWindow navigation: history, recent items, favorites, breadcrumb (M20 P0#1).
#include "directorytree.h"
#include "mainwindow_p.h"

void MainWindow::scheduleSidecarImport(const QString &dir)
{
    TaskScheduler::cancel(m_sidecarImportTask);
    if (m_sidecarImportAlive)
        m_sidecarImportAlive->store(false, std::memory_order_release);

    auto alive = std::make_shared<std::atomic<bool>>(true);
    m_sidecarImportAlive = alive;
    const std::string utf8Dir = dir.toUtf8().toStdString();
    const QString expectedDir = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    const QPointer<MainWindow> guard(this);
    m_sidecarImportTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [alive, utf8Dir](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled() || !alive->load(std::memory_order_acquire))
                return;
            mviewer::core::SidecarStore::instance().importDirectory(
                utf8Dir, [alive, &context]
                { return context.isCancelled() || !alive->load(std::memory_order_acquire); });
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, expectedDir]()
        {
            if (!guard || !qApp || !alive->load(std::memory_order_acquire))
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, expectedDir]()
                {
                    if (!guard || !alive->load(std::memory_order_acquire) || !guard->m_directory ||
                        QDir::cleanPath(QDir::fromNativeSeparators(
                            guard->m_directory->currentDirectory())) != expectedDir)
                        return;
                    // Rating/flag filters read RatingStore at evaluation time.
                    // Re-run the active filter after import so a sidecar never
                    // requires a manual refresh to converge.
                    guard->invalidateBrowseRatings();
                    if (guard->m_metadataPanel && !guard->currentImagePath().isEmpty())
                        guard->m_metadataPanel->setImage(guard->currentImagePath());
                },
                Qt::QueuedConnection);
        });
}

void MainWindow::scheduleSidecarImportPaths(const QStringList &imagePaths)
{
    if (imagePaths.isEmpty())
        return;
    TaskScheduler::cancel(m_sidecarImportTask);
    if (m_sidecarImportAlive)
        m_sidecarImportAlive->store(false, std::memory_order_release);

    auto alive = std::make_shared<std::atomic<bool>>(true);
    m_sidecarImportAlive = alive;
    const QString expectedDir = currentDir();
    const QPointer<MainWindow> guard(this);
    std::vector<std::string> paths;
    paths.reserve(static_cast<size_t>(imagePaths.size()));
    for (const QString &path : imagePaths)
        paths.push_back(path.toUtf8().toStdString());
    m_sidecarImportTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [alive, paths = std::move(paths)](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled() || !alive->load(std::memory_order_acquire))
                return;
            for (const std::string &path : paths)
            {
                if (context.isCancelled() || !alive->load(std::memory_order_acquire))
                    return;
                mviewer::core::SidecarStore::instance().readSidecar(path);
            }
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, expectedDir, imagePaths]()
        {
            if (!guard || !qApp || !alive->load(std::memory_order_acquire))
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, expectedDir, imagePaths]()
                {
                    if (!guard || !alive->load(std::memory_order_acquire) ||
                        !DirectoryTree::equivalentPath(guard->currentDir(), expectedDir))
                        return;
                    if (guard->m_thumbnailPanel)
                        guard->m_thumbnailPanel->refreshSidecarPaths(imagePaths);
                    if (guard->m_metadataPanel && !guard->currentImagePath().isEmpty())
                        guard->m_metadataPanel->setImage(guard->currentImagePath());
                },
                Qt::QueuedConnection);
        });
}

void MainWindow::navigate(int delta)
{
    if (currentDir().isEmpty() || currentImagePath().isEmpty())
        return;

    // M19: ImageListModel is the SSOT for the directory listing.
    ensureImageList();
    const QStringList list = m_imageList->paths();
    if (list.isEmpty())
        return;

    const int idx = m_imageList->indexOf(currentImagePath());
    // Wrap around at both ends (FastStone/ImageGlass parity; also keeps the
    // slideshow advancing past the last image).
    // A filter may remove the currently displayed image. The next navigation
    // action must enter the current visible sequence, not skip its first item
    // by pretending the removed image was row zero.
    const int next =
        idx < 0 ? (delta < 0 ? list.size() - 1 : 0) : (idx + delta + list.size()) % list.size();

    const QString path = list.at(next);
    // P0-2: single source of truth. onCurrentImageChanged() now also keeps the
    // thumbnail-grid highlight in sync with keyboard navigation — previously the
    // grid selection lagged behind the viewer when using the arrow keys.
    m_selection->setCurrentImage(path);
    // A manual step during a show should dwell for a full interval. The
    // slideshow's own tick also lands here; restarting only resets that dwell.
    if (m_slideshowTimer && m_slideshowTimer->isActive())
        m_slideshowTimer->start();
}

QString MainWindow::activeBrowseDirectory() const
{
    if (secondaryNavActive() && m_secondaryThumbnailPanel)
        return m_secondaryThumbnailPanel->currentDir();
    return currentDir();
}

void MainWindow::refreshActiveBrowse()
{
    if (m_directoryTree)
        m_directoryTree->refresh();
    if (secondaryNavActive())
    {
        if (m_secondaryThumbnailPanel)
            m_secondaryThumbnailPanel->refresh();
        if (m_secondaryDirectoryMonitor)
            m_secondaryDirectoryMonitor->reconcileNow();
        return;
    }
    if (m_directoryMonitor)
        m_directoryMonitor->reconcileNow();
}

void MainWindow::updateNavigationActions()
{
    const QString navDir = activeBrowseDirectory();
    const bool hasLeftDirectory = !currentDir().isEmpty();
    const bool hasActiveDirectory = !navDir.isEmpty();
    // Sidebar refresh stays available while either pane has a folder. Open
    // and favorite follow the pane the toolbar is acting on.
    const bool hasDirectory = hasLeftDirectory || hasActiveDirectory;
    if (m_actHistoryBack)
        m_actHistoryBack->setEnabled(m_historyIndex > 0);
    if (m_actHistoryForward)
        m_actHistoryForward->setEnabled(m_historyIndex >= 0 &&
                                        m_historyIndex + 1 < m_history.size());
    if (m_actDirBack)
        m_actDirBack->setEnabled(m_dirHistoryIndex > 0);
    if (m_actDirForward)
        m_actDirForward->setEnabled(m_dirHistoryIndex >= 0 &&
                                    m_dirHistoryIndex + 1 < m_dirHistory.size());
    if (m_actDirUp)
    {
        QDir parent(navDir);
        m_actDirUp->setEnabled(hasActiveDirectory && parent.cdUp());
    }
    updateSecondaryNavButtons();
    if (m_actRefresh)
        m_actRefresh->setEnabled(hasDirectory);
    if (m_actOpenCurrentFolder)
        m_actOpenCurrentFolder->setEnabled(hasActiveDirectory);
    const bool isFavorite = m_directory && m_directory->hasFavorite(navDir);
    if (m_actAddFavorite)
        m_actAddFavorite->setEnabled(hasActiveDirectory && !isFavorite);
    if (m_actRemoveFavorite)
        m_actRemoveFavorite->setEnabled(hasActiveDirectory && isFavorite);
}

void MainWindow::navigatePage(int key)
{
    if (currentDir().isEmpty())
        return;
    ensureImageList();
    const QStringList list = m_imageList->paths();
    if (list.isEmpty())
        return;

    const int idx = m_imageList->indexOf(currentImagePath());
    // A filter may remove the current image. Pretending it was row zero and
    // then stepping a page skips the first visible page. Enter that page
    // (Home / PageUp / PageDown) or the last image (End) instead.
    const bool visible = idx >= 0;
    constexpr int kPage = 10; // images per PageUp/PageDown step
    int target = 0;
    switch (key)
    {
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = list.size() - 1;
        break;
    case Qt::Key_PageUp:
        target = visible ? qMax(0, idx - kPage) : 0;
        break;
    case Qt::Key_PageDown:
        target = visible ? qMin(list.size() - 1, idx + kPage) : 0;
        break;
    default:
        return;
    }
    if (!visible || target != idx)
    {
        m_selection->setCurrentImage(list.at(target));
        if (m_slideshowTimer && m_slideshowTimer->isActive())
            m_slideshowTimer->start();
    }
}

void MainWindow::onBreadcrumbPath(const QString &path)
{
    // The committed directoryChanged signal owns all transition side effects.
    if (!path.isEmpty())
        changeDirectory(path);
}

void MainWindow::pushHistory(const QString &path)
{
    if (path.isEmpty())
        return;
    // Drop any "forward" entries when a new navigation occurs (browser semantics).
    if (m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    if (!m_history.isEmpty() && DirectoryTree::equivalentPath(m_history.last(), path))
        return; // no duplicate of the current tip
    m_history.append(path);
    m_historyIndex = m_history.size() - 1;
}

void MainWindow::navigateHistory(int delta)
{
    if (m_history.isEmpty())
        return;
    const int next = m_historyIndex + delta;
    if (next < 0 || next >= m_history.size())
        return;
    m_historyIndex = next;
    const QString path = m_history.at(next);
    // Re-open without pushing again (pushHistory is a no-op for the same tip).
    m_selection->setCurrentImage(path);
    m_imageViewer->setImage(path);  // async; imageReady() feeds AnalysisPanel
    m_previewPanel->setImage(path); // async; off UI thread
    statusBar()->showMessage(tr("Current: %1").arg(QFileInfo(path).fileName()), 3000);
    updateSelectionActions();
}

// P0: Directory-level back/forward history (independent of image history).
void MainWindow::pushDirHistory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    // Back/Forward changes the cursor before DirectoryTree emits the committed
    // transition. Recognize that existing entry before pruning the forward
    // branch, otherwise a Back operation destroys the very Forward entry it
    // should restore.
    if (m_dirHistoryIndex >= 0 && m_dirHistoryIndex < m_dirHistory.size() &&
        DirectoryTree::equivalentPath(m_dirHistory.at(m_dirHistoryIndex), dir))
        return;
    // Prune forward entries when branching.
    if (m_dirHistoryIndex >= 0 && m_dirHistoryIndex + 1 < m_dirHistory.size())
        m_dirHistory.erase(m_dirHistory.begin() + m_dirHistoryIndex + 1, m_dirHistory.end());
    // Suppress consecutive duplicates. Tree context menus and navigateTo can
    // disagree on '\' vs '/'; raw equality made Back a no-op on the twin entry.
    if (!m_dirHistory.isEmpty() && DirectoryTree::equivalentPath(m_dirHistory.last(), dir))
        return;
    m_dirHistory.append(dir);
    m_dirHistoryIndex = m_dirHistory.size() - 1;

    // Cap history size to avoid unbounded growth.
    constexpr int kMaxDirHistory = 50;
    while (m_dirHistory.size() > kMaxDirHistory)
    {
        m_dirHistory.removeFirst();
        --m_dirHistoryIndex;
    }
}

void MainWindow::goDirBack()
{
    if (secondaryNavActive())
    {
        navigateSecondaryBack();
        return;
    }
    if (m_dirHistoryIndex > 0)
    {
        --m_dirHistoryIndex;
        const QString dir = m_dirHistory.at(m_dirHistoryIndex);
        // Navigate without pushing to history (changeDirectory->pushDirHistory is guarded
        // by duplicate check). Use emitSignal=true to trigger the full update chain.
        m_directoryTree->navigateTo(dir, true);
        return;
    }
    // Fallback: if no back history exists in session, ascend to parent directory
    if (m_dirHistory.size() <= 1)
    {
        QDir parent(currentDir());
        if (!currentDir().isEmpty() && parent.cdUp())
            changeDirectory(parent.absolutePath());
    }
}

void MainWindow::goDirForward()
{
    if (secondaryNavActive())
    {
        navigateSecondaryForward();
        return;
    }
    if (m_dirHistoryIndex < 0 || m_dirHistoryIndex + 1 >= m_dirHistory.size())
        return;
    ++m_dirHistoryIndex;
    const QString dir = m_dirHistory.at(m_dirHistoryIndex);
    m_directoryTree->navigateTo(dir, true);
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const auto &p : m_recent.items())
    {
        const QString qs = QString::fromStdString(p);
        const QString fn = QFileInfo(qs).fileName();
        const QString label = fn.isEmpty() ? QDir::toNativeSeparators(qs) : fn;
        auto *act = m_recentMenu->addAction(label);
        act->setToolTip(qs);
        connect(
            act, &QAction::triggered, this,
            [this, qs]()
            {
                if (!QDir(qs).exists())
                {
                    m_recent.remove(qs.toStdString());
                    m_appState.recentFolders.removeAll(qs);
                    if (m_directory)
                        m_directory->removeRecentFolder(qs);
                    m_appState.save();
                    rebuildRecentMenu();
                    if (statusBar())
                        statusBar()->showMessage(
                            tr("Directory does not exist, removed from recent records: %1").arg(qs),
                            3000);
                    return;
                }
                changeDirectory(qs);
            });
    }
    if (!m_recent.items().empty())
    {
        m_recentMenu->addSeparator();
        auto *pruneAct = m_recentMenu->addAction(tr("Clean Invalid Directories"));
        connect(pruneAct, &QAction::triggered, this,
                [this]()
                {
                    int pruned = 0;
                    for (const auto &p : m_recent.items())
                    {
                        const QString qs = QString::fromStdString(p);
                        if (!QDir(qs).exists())
                        {
                            m_recent.remove(p);
                            m_appState.recentFolders.removeAll(qs);
                            if (m_directory)
                                m_directory->removeRecentFolder(qs);
                            ++pruned;
                        }
                    }
                    if (pruned > 0)
                    {
                        m_appState.save();
                        rebuildRecentMenu();
                    }
                    if (statusBar())
                        statusBar()->showMessage(
                            tr("Cleared %1 stale directory entries").arg(pruned), 2000);
                });
        auto *clearAct = m_recentMenu->addAction(tr("Clear Recent Directories"));
        connect(clearAct, &QAction::triggered, this,
                [this]()
                {
                    m_recent.clear();
                    m_appState.clearRecentFolders();
                    if (m_directory)
                        m_directory->setRecentFolders({});
                    rebuildRecentMenu();
                    if (statusBar())
                        statusBar()->showMessage(tr("Recent directory history cleared"), 2000);
                });
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction(tr("(None)"))->setEnabled(false);
}

void MainWindow::rebuildRecentFilesMenu()
{
    if (!m_recentFileMenu)
        return;
    // Initial construction already creates an empty QMenu. Avoid a native
    // QMenu::clear() during headless/early-window setup; on some Windows Qt
    // styles that path waits for menu teardown before the first event loop.
    if (m_recentFiles.items().empty() && m_recentFileMenu->actions().isEmpty())
    {
        m_recentFileMenu->addAction(tr("(None)"))->setEnabled(false);
        return;
    }
    m_recentFileMenu->clear();
    for (const auto &p : m_recentFiles.items())
    {
        const QString qs = QString::fromStdString(p);
        const QString fn = QFileInfo(qs).fileName();
        const QString label = fn.isEmpty() ? QDir::toNativeSeparators(qs) : fn;
        auto *act = m_recentFileMenu->addAction(label);
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this,
                [this, qs]()
                {
                    if (!QFile::exists(qs))
                    {
                        m_recentFiles.remove(qs.toUtf8().toStdString());
                        m_appState.history.removeAll(qs);
                        m_appState.save();
                        rebuildRecentFilesMenu();
                        if (statusBar())
                            statusBar()->showMessage(
                                tr("File missing; removed from recent list: %1").arg(qs), 3000);
                        return;
                    }
                    onImageOpen(qs);
                });
    }
    if (!m_recentFiles.items().empty())
    {
        m_recentFileMenu->addSeparator();
        auto *pruneAct = m_recentFileMenu->addAction(tr("Clean Invalid Files"));
        connect(pruneAct, &QAction::triggered, this,
                [this]()
                {
                    int pruned = 0;
                    for (const auto &p : m_recentFiles.items())
                    {
                        const QString qs = QString::fromStdString(p);
                        if (!QFile::exists(qs))
                        {
                            m_recentFiles.remove(p);
                            m_appState.history.removeAll(qs);
                            ++pruned;
                        }
                    }
                    if (pruned > 0)
                    {
                        m_appState.save();
                        rebuildRecentFilesMenu();
                    }
                    if (statusBar())
                        statusBar()->showMessage(tr("Cleaned %1 invalid file records").arg(pruned),
                                                 2000);
                });
        auto *clearAct = m_recentFileMenu->addAction(tr("Clear recent files"));
        connect(clearAct, &QAction::triggered, this,
                [this]()
                {
                    m_recentFiles.clear();
                    m_appState.clearHistory();
                    rebuildRecentFilesMenu();
                    if (statusBar())
                        statusBar()->showMessage(tr("Recent file history cleared"), 2000);
                });
    }
    if (m_recentFileMenu->isEmpty())
        m_recentFileMenu->addAction(tr("(None)"))->setEnabled(false);
}

void MainWindow::rebuildFavoritesMenu()
{
    if (!m_favMenu)
        return;
    m_favMenu->clear();
    for (const auto &qs : m_appState.favorites)
    {
        auto *act = m_favMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { changeDirectory(qs); });
    }
    if (m_favMenu->isEmpty())
        m_favMenu->addAction(tr("(None)"))->setEnabled(false);
}

void MainWindow::addFavoriteCurrent()
{
    const QString dir = activeBrowseDirectory();
    if (dir.isEmpty())
    {
        statusBar()->showMessage(tr("No folder to add to favorites"), 3000);
        return;
    }
    m_appState.addFavorite(dir);
    m_directory->addFavorite(dir);
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    updateSelectionActions();
    statusBar()->showMessage(tr("Added to favorites: %1").arg(QFileInfo(dir).fileName()), 3000);
}

void MainWindow::removeFavorite(const QString &dir)
{
    const QString target = dir.isEmpty() ? activeBrowseDirectory() : dir;
    if (target.isEmpty())
        return;
    m_appState.removeFavorite(target);
    m_directory->removeFavorite(target);
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    updateSelectionActions();
    statusBar()->showMessage(tr("Removed from favorites: %1").arg(QFileInfo(target).fileName()),
                             3000);
}

void MainWindow::rebuildFavoritesBar()
{
    if (!m_favoritesBar)
        return;
    m_favoritesBar->clear();
    for (const auto &qs : m_appState.favorites)
    {
        auto *item = new QListWidgetItem(QFileInfo(qs).fileName());
        item->setData(Qt::UserRole, qs);
        item->setToolTip(qs);
        m_favoritesBar->addItem(item);
    }
}

void MainWindow::clearAllFilters()
{
    if (m_searchEdit)
        m_searchEdit->clear();
    if (m_ratingFilter)
        m_ratingFilter->setCurrentIndex(0);
    if (m_flagFilter)
        m_flagFilter->setCurrentIndex(0);
    eachFilterPanel(
        [](ThumbnailPanel *panel)
        {
            panel->clearFlagFilters();
            panel->setRatingFilter(0);
            panel->setFilter(QString(), false);
        });
    statusBar()->showMessage(tr("All filters cleared"), 2000);
}

void MainWindow::resetFiltersOnFolderChange()
{
    const QSignalBlocker b1(m_searchEdit);
    const QSignalBlocker b2(m_ratingFilter);
    const QSignalBlocker b3(m_flagFilter);
    if (m_searchEdit)
        m_searchEdit->clear();
    if (m_ratingFilter)
        m_ratingFilter->setCurrentIndex(0);
    if (m_flagFilter)
        m_flagFilter->setCurrentIndex(0);
    eachFilterPanel(
        [](ThumbnailPanel *panel)
        {
            panel->clearFlagFilters();
            panel->setRatingFilter(0);
            panel->setFilter(QString(), false);
        });
}

void MainWindow::applyFlagFilterToPanel(ThumbnailPanel *panel, int rating, int flag)
{
    if (!panel)
        return;
    panel->clearFlagFilters();
    panel->setRatingFilter(rating);
    switch (flag)
    {
    case 1:
        panel->setPickFilter(true);
        break;
    case 2:
        panel->setRejectFilter(true);
        break;
    case 3:
        panel->setRecentFilter(true);
        break;
    case 11:
        panel->setLabelFilter(1);
        break;
    case 12:
        panel->setLabelFilter(2);
        break;
    case 13:
        panel->setLabelFilter(3);
        break;
    case 14:
        panel->setLabelFilter(4);
        break;
    case 15:
        panel->setLabelFilter(5);
        break;
    case 16:
        panel->setLabelFilter(6);
        break;
    default:
        break;
    }
}

void MainWindow::invalidateBrowseRatings()
{
    eachBrowsePanel([](ThumbnailPanel *panel) { panel->invalidateRatings(); });
}

void MainWindow::applySharedSearchFilter()
{
    const QString text = m_searchEdit ? m_searchEdit->text() : QString();
    const bool recursive = m_searchRecursive && m_searchRecursive->isChecked();
    eachFilterPanel([text, recursive](ThumbnailPanel *panel)
                    { panel->setFilter(text, recursive); });
}
