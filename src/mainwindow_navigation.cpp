// MainWindow navigation: history, recent items, favorites, breadcrumb (M20 P0#1).
#include "mainwindow_p.h"

void MainWindow::scheduleSidecarImport(const QString &dir)
{
    TaskScheduler::cancel(m_sidecarImportTask);
    if (m_sidecarImportAlive)
        m_sidecarImportAlive->store(false, std::memory_order_release);

    auto alive = std::make_shared<std::atomic<bool>>(true);
    m_sidecarImportAlive = alive;
    const std::string utf8Dir = dir.toUtf8().toStdString();
    m_sidecarImportTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [alive, utf8Dir](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled() || !alive->load(std::memory_order_acquire))
                return;
            mviewer::core::SidecarStore::instance().importDirectory(
                utf8Dir,
                [alive, &context]
                {
                    return context.isCancelled() || !alive->load(std::memory_order_acquire);
                });
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

    const int idx = list.indexOf(currentImagePath());
    // Wrap around at both ends (FastStone/ImageGlass parity; also keeps the
    // slideshow advancing past the last image).
    // A filter may remove the currently displayed image. The next navigation
    // action must enter the current visible sequence, not skip its first item
    // by pretending the removed image was row zero.
    const int next = idx < 0
                         ? (delta < 0 ? list.size() - 1 : 0)
                         : (idx + delta + list.size()) % list.size();

    const QString path = list.at(next);
    // P0-2: single source of truth. onCurrentImageChanged() now also keeps the
    // thumbnail-grid highlight in sync with keyboard navigation — previously the
    // grid selection lagged behind the viewer when using the arrow keys.
    m_selection->setCurrentImage(path);
}

void MainWindow::navigatePage(int key)
{
    if (currentDir().isEmpty())
        return;
    ensureImageList();
    const QStringList list = m_imageList->paths();
    if (list.isEmpty())
        return;

    int idx = list.indexOf(currentImagePath());
    if (idx < 0)
        idx = 0;
    constexpr int kPage = 10; // images per PageUp/PageDown step
    int target = idx;
    switch (key)
    {
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = list.size() - 1;
        break;
    case Qt::Key_PageUp:
        target = qMax(0, idx - kPage);
        break;
    case Qt::Key_PageDown:
        target = qMin(list.size() - 1, idx + kPage);
        break;
    default:
        return;
    }
    if (target != idx || currentImagePath().isEmpty())
        m_selection->setCurrentImage(list.at(target));
}

void MainWindow::onBreadcrumbPath(const QString &path)
{
    // M15 Product Shell P0: navigate the directory tree to the breadcrumb path.
    if (!path.isEmpty())
    {
        m_directoryTree->navigateTo(path, true);
        pushDirHistory(path);
    }
}

void MainWindow::pushHistory(const QString &path)
{
    if (path.isEmpty())
        return;
    // Drop any "forward" entries when a new navigation occurs (browser semantics).
    if (m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    if (!m_history.isEmpty() && m_history.last() == path)
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
    statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
}

// P0: Directory-level back/forward history (independent of image history).
void MainWindow::pushDirHistory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    // Prune forward entries when branching.
    if (m_dirHistoryIndex >= 0 && m_dirHistoryIndex + 1 < m_dirHistory.size())
        m_dirHistory.erase(m_dirHistory.begin() + m_dirHistoryIndex + 1, m_dirHistory.end());
    // Suppress consecutive duplicates.
    if (!m_dirHistory.isEmpty() && m_dirHistory.last() == dir)
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
    if (m_dirHistoryIndex <= 0)
        return;
    --m_dirHistoryIndex;
    const QString dir = m_dirHistory.at(m_dirHistoryIndex);
    // Navigate without pushing to history (changeDirectory->pushDirHistory is guarded
    // by duplicate check). Use emitSignal=true to trigger the full update chain.
    m_directoryTree->navigateTo(dir, true);
}

void MainWindow::goDirForward()
{
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
        auto *act = m_recentMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { changeDirectory(qs); });
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction("(无)")->setEnabled(false);
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
        m_recentFileMenu->addAction("(无)")->setEnabled(false);
        return;
    }
    m_recentFileMenu->clear();
    for (const auto &p : m_recentFiles.items())
    {
        const QString qs = QString::fromStdString(p);
        auto *act = m_recentFileMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { onImageOpen(qs); });
    }
    if (m_recentFileMenu->isEmpty())
        m_recentFileMenu->addAction("(无)")->setEnabled(false);
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
        m_favMenu->addAction("(无)")->setEnabled(false);
}

void MainWindow::addFavoriteCurrent()
{
    if (currentDir().isEmpty())
    {
        statusBar()->showMessage("没有可收藏的目录");
        return;
    }
    m_appState.addFavorite(currentDir());
    m_directory->addFavorite(currentDir());
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    statusBar()->showMessage(QString("已收藏: %1").arg(currentDir()));
}

void MainWindow::removeFavorite(const QString &dir)
{
    const QString target = dir.isEmpty() ? currentDir() : dir;
    if (target.isEmpty())
        return;
    m_appState.removeFavorite(target);
    m_directory->removeFavorite(target);
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    statusBar()->showMessage(QString("已取消收藏: %1").arg(QFileInfo(target).fileName()));
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
