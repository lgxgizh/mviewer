// MainWindow signal wiring split by workflow responsibility.
#include "mainwindow_p.h"

void MainWindow::connectUiSignals()
{
    connectNavigationSignals();
    connectGallerySignals();
    connectSelectionSignals();
    connectViewerSignals();
    connectFilterSignals();
    connectMenuSignals();
    connectWorkspaceSignals();
    connectPanelSignals();
    connectSettingsSignals();
}

void MainWindow::connectNavigationSignals()
{
    // ----- Signals -----
    connect(m_directoryTree, &DirectoryTree::directoryChanged, m_thumbnailPanel,
            &ThumbnailPanel::setDirectory);
    // M37: the asynchronous gallery scan publishes the final visible sequence
    // into the single navigation model. Viewer, keyboard navigation, Compare
    // and preload all consume this same ordered list.
    connect(m_thumbnailPanel, &ThumbnailPanel::sequenceChanged, this,
            [this](const QString &directory, const QStringList &paths)
            {
                if (!m_imageList)
                    return;
                m_imageList->setPaths(paths, directory);
                if (m_directory && m_directory->currentDirectory() == directory)
                    statusBar()->showMessage(
                        QStringLiteral("Browse: %1, %2 images").arg(directory).arg(paths.size()));
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
                if (m_selection)
                    m_selection->clear();
                m_breadcrumb->setPath(path); // M15: update breadcrumb bar
                if (m_pathEdit)
                    m_pathEdit->setText(QDir::toNativeSeparators(path));
                // M19: DirectoryModel + ImageListModel are the SSOT.
                m_directory->setCurrentDirectory(path);
                m_workspace->setRootPath(path);
                // ThumbnailPanel clears and later publishes the final sequence.
                // Do not enumerate the directory synchronously here.
                // P0-1: record this folder in the recent-folders LRU + repopulate
                // the Recent menu.
                m_recent.add(path.toStdString());
                m_appState.addRecentFolder(path);
                m_directory->addRecentFolder(path);
                rebuildRecentMenu();
                // P0: push directory-level history for back/forward navigation.
                pushDirHistory(path);
                statusBar()->showMessage(QStringLiteral("Browse: %1, scanning…").arg(path));
                // With no image selected yet, the title carries the folder.
                if (currentImagePath().isEmpty())
                    setWindowTitle(QString("%1 - MViewer").arg(QDir(path).dirName()));
                scheduleReindex();
                if (m_emptyFolderTimer)
                    m_emptyFolderTimer->stop();
                if (m_emptyFolderLabel)
                    m_emptyFolderLabel->hide();
                updateEmptyState();
            });
}

void MainWindow::connectGallerySignals()
{
    connect(m_thumbnailPanel, &ThumbnailPanel::statsChanged, this,
            [this](int total, qint64, int, qint64)
            {
                // Empty-folder feedback: once the gallery settles with
                // zero displayable entries, defer showing the hint past the
                // transient pre-scan zero; any real content cancels it.
                if (total > 0)
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
                if (!m_autoSelectFirstPending || total <= 0 || !m_selection ||
                    !m_thumbnailPanel)
                    return;
                if (!m_selection->currentImage().isEmpty())
                {
                    m_autoSelectFirstPending = false;
                    return;
                }
                const QString first = m_thumbnailPanel->pathList().value(0);
                if (first.isEmpty())
                    return;
                m_autoSelectFirstPending = false;
                m_selection->setCurrentImage(first);
            });
}

void MainWindow::connectSelectionSignals()
{
    connect(m_thumbnailPanel, &ThumbnailPanel::itemClicked, this,
            [this](const QString &path)
            {
                // P0-2: route selection through the shared model; all panels are
                // updated centrally in onCurrentImageChanged().
                // Skip while model→gallery sync is in progress (selectPaths
                // already owns the multi-select); also skip empty paths.
                if (m_syncingSelection || path.isEmpty() || !m_selection)
                    return;
                m_selection->setCurrentImage(path);
            });
    // P0-2: the single place that keeps every view in sync with the current
    // image. Connected once; fired whenever the selection model changes,
    // regardless of the source (thumbnail click, keyboard nav, open, restore).
    connect(m_selection, &SelectionModel::currentImageChanged, this,
            &MainWindow::onCurrentImageChanged);
    connect(m_selection, &SelectionModel::currentImageChanged, this,
            [this](const QString &path)
            {
                if (!path.isEmpty())
                    return;
                if (m_previewPanel)
                    m_previewPanel->setImage({});
                if (m_metadataPanel)
                    m_metadataPanel->setImage({});
                if (m_metadataOverlay)
                    m_metadataOverlay->hide();
                if (m_actToggleMetadata)
                    m_actToggleMetadata->setChecked(false);
                if (m_lblImage)
                    m_lblImage->setText(tr("未选择图像"));
                const QString activeDir =
                    m_directory ? m_directory->currentDirectory() : QString();
                setWindowTitle(activeDir.isEmpty()
                                   ? QStringLiteral("MViewer")
                                   : QString("%1 - MViewer").arg(QDir(activeDir).dirName()));
                updateSelectionActions();
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpen(path); });
    connect(m_thumbnailPanel, &ThumbnailPanel::compareRequested, this,
            [this](const QStringList &images) { openCompare(images); });
    // A-3 / M19: keep SelectionModel multi-selection in lock-step with the
    // gallery. Guarded by m_syncingSelection so model→gallery sync does not
    // re-enter and overwrite a programmatic selection.
    connect(m_thumbnailPanel->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]()
            {
                if (m_syncingSelection || !m_selection || !m_thumbnailPanel)
                    return;
                const QStringList paths = m_thumbnailPanel->selectedPaths();
                if (paths.isEmpty())
                {
                    updateSelectionActions();
                    return;
                }
                const QString cur =
                    m_thumbnailPanel->currentIndex().isValid()
                        ? m_thumbnailPanel->pathList().value(m_thumbnailPanel->currentIndex().row())
                        : paths.first();
                m_selection->setSelection(paths, cur);
                updateSelectionActions();
            });
    // A-3.4 / M19: SelectionModel → gallery (full multi-select, not just current).
    connect(m_selection, &SelectionModel::selectionChanged, this,
            [this](const QStringList &)
            {
                syncGalleryFromSelection();
                updateSelectionActions();
            });
    // Dropping files directly onto the gallery behaves the same as dropping
    // them anywhere else on the window.
    connect(m_thumbnailPanel, &ThumbnailPanel::filesDropped, this, &MainWindow::handleDroppedPaths);
    // When the user deletes images from the gallery, advance the viewer off the
    // deleted image if it was the one being viewed.
    connect(m_thumbnailPanel, &ThumbnailPanel::pathsRemoved, this,
            [this](const QStringList &deleted)
            {
                m_imageList->removePaths(deleted);
                if (currentImagePath().isEmpty() || m_imageViewer->isHidden())
                    return;
                if (!deleted.contains(currentImagePath()))
                    return;
                // Advance to the next available image in the (refreshed) folder.
                navigate(1);
            });

    // M27: the previous EventBus subscriptions ("image.open" / "compare.requested")
    // had NO publishers anywhere in the codebase — dead code. Removed; the
    // UI signals (thumbnail double-click, menu actions) drive these paths
    // directly via MainWindow's own slots.
}

void MainWindow::connectViewerSignals()
{
    connect(m_imageViewer, &ImageViewer::regionStats, m_analysisPanel,
            &AnalysisPanel::setRegionStats);
    // P0续: feed the decoded ImageFrame to the analysis panel once the async
    // load completes (no re-decode on the UI thread). This replaces the old
    // synchronous QImage(path) decode that blocked browsing.
    connect(m_imageViewer, &ImageViewer::imageReady, m_analysisPanel, &AnalysisPanel::setFrame);
    // P0-3: an active metadata overlay follows the freshly decoded frame — its
    // histogram is computed on the Analysis pool only after the frame is ready
    // (navigating while the overlay is visible must never show the old image's
    // histogram; the delivery is also generation/path/frame-guarded).
    connect(m_imageViewer, &ImageViewer::imageReady, this,
            [this](const std::shared_ptr<ImageFrame> &)
            {
                if (m_metadataOverlay && m_metadataOverlay->isVisible())
                    scheduleMetadataHistogram();
            });
    // M12.2 (G2-ext): also record each image's analysis result per-path so the
    // whole compare session's analysis context can be persisted into the .mvws.
    connect(m_imageViewer, &ImageViewer::regionStats, this,
            [this](const QString &text)
            {
                // M19: AnalyzerModel owns per-image results (capped + history).
                if (!currentImagePath().isEmpty())
                    m_analyzer->setResult(currentImagePath(), text);
            });
    connect(m_imageViewer, &ImageViewer::selectionChanged, m_analysisPanel,
            [this](const QRect &sel)
            {
                if (sel.isEmpty())
                    return;
                mviewer::domain::Selection roi;
                roi.x = sel.x();
                roi.y = sel.y();
                roi.width = sel.width();
                roi.height = sel.height();
                m_analysisPanel->setROI(roi);
            });
    connect(m_imageViewer, &ImageViewer::requestPrev, this, [this]() { navigate(-1); });
    connect(m_imageViewer, &ImageViewer::requestNext, this, [this]() { navigate(1); });
    connect(m_imageViewer, &ImageViewer::viewerClosed, this,
            [this]()
            {
                if (!isVisible())
                    return;
                raise();
                activateWindow();
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setFocus(Qt::OtherFocusReason);
            });
    // A-7.3: viewer context-menu "分析" → show panel + run through unified entry.
    connect(m_imageViewer, &ImageViewer::analysisRequested, this,
            [this](const QString &analyzerId)
            {
                if (!m_analysisPanel)
                    return;
                m_analysisPanel->setVisible(true);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(true);
                m_analysisPanel->runAnalyzer(analyzerId);
            });
    connect(m_imageViewer, &ImageViewer::pixelInfo, this,
            [this](int x, int y, int r, int g, int b, int a, int r16, int g16, int b16, int rawKind,
                   bool valid)
            {
                if (valid)
                {
                    if (a < 255)
                        statusBar()->showMessage(
                            QString("像素 [%1,%2]  RGBA(%3,%4,%5,%6)  16bit(%7,%8,%9)")
                                .arg(x)
                                .arg(y)
                                .arg(r)
                                .arg(g)
                                .arg(b)
                                .arg(a)
                                .arg(r16)
                                .arg(g16)
                                .arg(b16));
                    else
                        statusBar()->showMessage(
                            QString("像素 [%1,%2]  RGB(%3,%4,%5)  16bit(%6,%7,%8)")
                                .arg(x)
                                .arg(y)
                                .arg(r)
                                .arg(g)
                                .arg(b)
                                .arg(r16)
                                .arg(g16)
                                .arg(b16));
                }
                else
                {
                    statusBar()->showMessage("光标不在图像上");
                }
                m_analysisPanel->showPixel(x, y, r, g, b, a, r16, g16, b16, rawKind, valid);
            });
}

void MainWindow::connectFilterSignals()
{
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                m_thumbnailPanel->setSortMode(
                    static_cast<ThumbnailPanel::SortMode>(m_sortCombo->currentData().toInt()));
            });

    // M18: live search → gallery filter (debounced via textChanged; recursive
    // checkbox re-applies immediately).
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &)
            { m_thumbnailPanel->setFilter(m_searchEdit->text(), m_searchRecursive->isChecked()); });
    connect(m_searchRecursive, &QCheckBox::toggled, this, [this](bool)
            { m_thumbnailPanel->setFilter(m_searchEdit->text(), m_searchRecursive->isChecked()); });
    // P1: metadata search toggle — re-applies the active filter against embedded
    // metadata instead of just filenames.
    connect(m_searchMeta, &QCheckBox::toggled, this, &MainWindow::onSearchMetaToggled);
    // P1: star-rating filter.
    connect(m_ratingFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onRatingFilterChanged);
    // P3 tail: color label / reject / pick / recents filter.
    connect(m_flagFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onFlagFilterChanged);
    // P0: MetadataPanel auto-syncs to SelectionModel (no manual push needed on image
    // change). Editing callbacks (rating/flags/toggle) still push explicitly for
    // immediate refresh after write — those are harmless redundancy, not SSOT drift.
    connect(m_selection, &SelectionModel::currentImageChanged, m_metadataPanel,
            &MetadataPanel::setImage);
    // P3 tail: a flag change in the metadata panel refreshes the gallery overlay
    // (and re-applies the active filter so list membership stays correct).
    connect(m_metadataPanel, &MetadataPanel::flagsEdited, this, &MainWindow::onFlagsEdited);
    // P1: a rating set in the metadata panel refreshes the gallery star overlay.
    connect(m_metadataPanel, &MetadataPanel::ratingEdited, this,
            [this](const QString &path, int)
            {
                Q_UNUSED(path);
                m_thumbnailPanel->invalidateRatings();
                // Re-apply the active filter so a rating change that moves an
                // image out of the filter range immediately removes it from the
                // gallery (and vice versa).
                m_thumbnailPanel->setRatingFilter(m_ratingFilter->currentData().toInt());
            });
}

void MainWindow::connectMenuSignals()
{
    // ----- Menu actions -----
    connect(m_actOpenDir, &QAction::triggered, this,
            [this]()
            {
                const QString dir = QFileDialog::getExistingDirectory(this, "打开目录");
                if (!dir.isEmpty())
                    changeDirectory(dir);
            });
    connect(m_actDirUp, &QAction::triggered, this,
            [this]()
            {
                QDir parent(currentDir());
                if (!currentDir().isEmpty() && parent.cdUp())
                    changeDirectory(parent.absolutePath());
            });
    connect(m_actRefresh, &QAction::triggered, this,
            [this]()
            {
                if (m_directoryTree)
                    m_directoryTree->refresh();
                if (m_thumbnailPanel)
                    m_thumbnailPanel->refresh();
                if (m_imageList)
                    m_imageList->markDirty();
                scheduleReindex();
            });

    // Path input bar: pressing Enter navigates to the typed directory.
    connect(m_pathEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                QString text = m_pathEdit->text().trimmed();
                if (text.isEmpty())
                    return;
                // Accept both native and forward-slash separators.
                text = QDir::fromNativeSeparators(text);
                QDir d(text);
                if (d.exists())
                    changeDirectory(QDir::cleanPath(text));
                else
                {
                    statusBar()->showMessage(QString("路径不存在: %1").arg(text), 5000);
                    // Restore the current path in the edit.
                    if (!currentDir().isEmpty())
                        m_pathEdit->setText(QDir::toNativeSeparators(currentDir()));
                }
            });
    connect(m_actOpenFile, &QAction::triggered, this,
            [this]()
            {
                // M25: the Open File filter is built from the format SSOT so it
                // can never drift from what the gallery/navigation list.
                QString filter = "图片文件 (";
                for (const auto &w : mviewer::core::ImageFormats::wildcardFilters())
                    filter += QString::fromStdString(w) + " ";
                filter = filter.trimmed() + ");;所有文件 (*)";
                const QString file = QFileDialog::getOpenFileName(
                    this, "打开图片", currentDir().isEmpty() ? QString() : currentDir(), filter);
                if (!file.isEmpty())
                    onImageOpen(file);
            });
    connect(m_actZoomIn, &QAction::triggered, this, [this]() { zoomViewer(0); });
    connect(m_actZoomOut, &QAction::triggered, this, [this]() { zoomViewer(1); });
    connect(m_actZoomFit, &QAction::triggered, this, [this]() { zoomViewer(2); });
    connect(m_actZoomActual, &QAction::triggered, this, [this]() { zoomViewer(3); });
    connect(m_actFullscreen, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    connect(m_actSlideshow, &QAction::triggered, this, &MainWindow::toggleSlideshow);
    // Surface decode failures instead of leaving them silent on the canvas.
    connect(m_imageViewer, &ImageViewer::loadFailed, this,
            [this](const QString &path)
            {
                statusBar()->showMessage(
                    QString("无法加载图片: %1").arg(QFileInfo(path).fileName()), 5000);
            });
    connect(m_actSaveWorkspace, &QAction::triggered, this, &MainWindow::saveWorkspace);
    connect(m_actOpenWorkspace, &QAction::triggered, this, &MainWindow::openWorkspace);
    connect(m_actSaveProject, &QAction::triggered, this, &MainWindow::saveProject);
    connect(m_actOpenProject, &QAction::triggered, this, &MainWindow::openProject);
    connect(m_actExportReport, &QAction::triggered, this, &MainWindow::exportReport);
    connect(m_actExportImages, &QAction::triggered, this, &MainWindow::exportImages);
    connect(m_actExit, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_actCompare, &QAction::triggered, this,
            [this]()
            {
                // M19: SelectionModel first; fall back to ImageListModel.
                QStringList imgs = resolveSelectedPaths(true);
                if (imgs.size() < 2)
                {
                    ensureImageList();
                    imgs = m_imageList ? m_imageList->paths() : QStringList();
                }
                if (imgs.isEmpty())
                {
                    const QString dir = QFileDialog::getExistingDirectory(this, tr("打开目录"));
                    if (!dir.isEmpty())
                    {
                        changeDirectory(dir);
                        ensureImageList();
                        imgs = m_imageList ? m_imageList->paths() : QStringList();
                    }
                }
                if (imgs.size() > 8)
                    imgs = imgs.mid(0, 8);
                if (!imgs.isEmpty())
                    openCompare(imgs);
            });
}

void MainWindow::connectWorkspaceSignals()
{
    auto syncBrowseWorkspaceAction = [this]()
    {
        if (m_actBrowseWorkspace && m_analysisPanel && m_searchPanel)
            m_actBrowseWorkspace->setChecked(!m_analysisPanel->isVisible() &&
                                              !m_searchPanel->isVisible());
    };
    connect(m_actToggleAnalysis, &QAction::toggled, this,
            [this, syncBrowseWorkspaceAction](bool visible)
            {
                m_analysisPanel->setVisible(visible);
                if (!visible && !m_searchPanel->isVisible())
                    syncBrowseWorkspaceAction();
                else if (m_actBrowseWorkspace)
                {
                    // Opening a panel is an explicit exit from Browse. Update
                    // the action state without replaying Browse's old restore
                    // snapshot over the user's newly opened panel.
                    const QSignalBlocker blocker(m_actBrowseWorkspace);
                    m_actBrowseWorkspace->setChecked(false);
                }
            });
    connect(m_actToggleSearch, &QAction::toggled, this,
            [this, syncBrowseWorkspaceAction](bool visible)
            {
                m_searchPanel->setVisible(visible);
                if (!visible && !m_analysisPanel->isVisible())
                    syncBrowseWorkspaceAction();
                else if (m_actBrowseWorkspace)
                {
                    const QSignalBlocker blocker(m_actBrowseWorkspace);
                    m_actBrowseWorkspace->setChecked(false);
                }
            });
    connect(m_actBrowseWorkspace, &QAction::toggled, this,
            [this](bool browse)
            {
                if (browse)
                {
                    m_browseAnalysisVisible = m_analysisPanel->isVisible();
                    m_browseSearchVisible = m_searchPanel->isVisible();
                    m_analysisPanel->hide();
                    m_searchPanel->hide();
                    m_actToggleAnalysis->setChecked(false);
                    m_actToggleSearch->setChecked(false);
                }
                else
                {
                    m_analysisPanel->setVisible(m_browseAnalysisVisible);
                    m_searchPanel->setVisible(m_browseSearchVisible);
                    m_actToggleAnalysis->setChecked(m_browseAnalysisVisible);
                    m_actToggleSearch->setChecked(m_browseSearchVisible);
                }
            });
    // restoreLastSession applies persisted panel visibility on a queued turn.
    // Re-sync after that turn so the browser action never lies about the shell.
    QTimer::singleShot(0, this, syncBrowseWorkspaceAction);
    QTimer::singleShot(250, this, syncBrowseWorkspaceAction);
    connect(m_actFocusBrowse, &QAction::triggered, this, &MainWindow::toggleFocusBrowse);
    // P0-3 / A-5: metadata toggle — show both the viewer overlay AND the floating
    // MetadataPanel (positioned on the right edge of the main window).
}

void MainWindow::connectPanelSignals()
{
    connect(m_actToggleMetadata, &QAction::triggered, this,
            [this](bool checked)
            {
                if (currentImagePath().isEmpty())
                    return;
                if (checked)
                {
                    if (m_metadataOverlay)
                        m_metadataOverlay->showForImage(currentImagePath());
                    if (m_metadataPanel)
                    {
                        positionMetadataPanel();
                        m_metadataPanel->show();
                        m_metadataPanel->raise();
                        // The panel is visibility-gated: show it before
                        // submitting the shared metadata request. A hidden
                        // panel records identity only and deliberately does no
                        // presentation work.
                        m_metadataPanel->setImage(currentImagePath());
                    }
                }
                else
                {
                    if (m_metadataOverlay)
                        m_metadataOverlay->hide();
                    if (m_metadataPanel)
                        m_metadataPanel->hide();
                }
            });
    connect(m_searchPanel, &SearchPanel::resultActivated, this,
            QOverload<const QString &>::of(&MainWindow::onImageOpen));
    connect(m_actBatch, &QAction::triggered, this,
            [this]()
            {
                if (!m_batchDialog)
                    m_batchDialog = new BatchDialog(this);
                // A-3: prefer SelectionModel multi-selection; fall back to
                // gallery selection, then the full directory list.
                QStringList inputs = resolveSelectedPaths(true);
                if (inputs.isEmpty())
                    inputs = m_imageList->paths();
                m_batchDialog->setInputFiles(inputs);
                m_batchDialog->exec();
            });
    connect(m_actPluginSettings, &QAction::triggered, this,
            [this]()
            {
                if (!m_pluginSettings)
                {
                    m_pluginSettings = new PluginSettings(this);
                    m_pluginSettings->setAttribute(Qt::WA_DeleteOnClose);
                    connect(m_pluginSettings, &QDialog::destroyed, this,
                            [this]() { m_pluginSettings = nullptr; });
                    // M17: after rescan / enable-toggle, refresh the analyzer combo
                    // so newly loaded plugins appear without restarting.
                    connect(m_pluginSettings, &PluginSettings::pluginsChanged, this,
                            [this]()
                            {
                                if (m_analysisPanel)
                                    m_analysisPanel->refreshAnalyzers();
                                statusBar()->showMessage(tr("插件列表已更新"), 3000);
                            });
                }
                m_pluginSettings->show();
                m_pluginSettings->raise();
                m_pluginSettings->activateWindow();
            });
}

void MainWindow::connectSettingsSignals()
{
    connect(m_actExportSettings, &QAction::triggered, this,
            [this]()
            {
                const QString path = QFileDialog::getSaveFileName(
                    this, tr("导出设置"), QString(), tr("MViewer 设置文件 (*.mvs);;所有文件 (*)"));
                if (path.isEmpty())
                    return;
                std::string err;
                if (mviewer::core::exportSettings(path.toStdString(), &err))
                    QMessageBox::information(this, tr("导出设置"), tr("设置已导出至 %1").arg(path));
                else
                    QMessageBox::warning(this, tr("导出设置"),
                                         tr("导出失败：%1").arg(QString::fromStdString(err)));
            });
    connect(m_actImportSettings, &QAction::triggered, this,
            [this]()
            {
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("导入设置"), QString(), tr("MViewer 设置文件 (*.mvs);;所有文件 (*)"));
                if (path.isEmpty())
                    return;
                std::string err;
                if (mviewer::core::importSettings(path.toStdString(), &err))
                {
                    QMessageBox::information(this, tr("导入设置"),
                                             tr("设置已导入。部分更改需要重启 MViewer 才能生效。"));
                }
                else
                    QMessageBox::warning(this, tr("导入设置"),
                                         tr("导入失败：%1").arg(QString::fromStdString(err)));
            });
    connect(
        m_actAbout, &QAction::triggered, this, [this]()
        {
            QMessageBox::about(
                this, "关于 MViewer",
                tr("MViewer %1\n\n图像算法工程师的可视化分析平台：浏览、对比、分析、导出。\n\n构建: %2")
                    .arg(QStringLiteral(MVIEWER_VERSION_STRING),
                         QStringLiteral(MVIEWER_VERSION_FULL)));
        });

    // P0: recent / favorites / history wiring.
    connect(m_actAddFavorite, &QAction::triggered, this, &MainWindow::addFavoriteCurrent);
    connect(m_actRemoveFavorite, &QAction::triggered, this, [this]() { removeFavorite(); });
    connect(m_actHistoryBack, &QAction::triggered, this, [this]() { navigateHistory(-1); });
    connect(m_actHistoryForward, &QAction::triggered, this, [this]() { navigateHistory(1); });
    connect(m_actDirBack, &QAction::triggered, this, &MainWindow::goDirBack);
    connect(m_actDirForward, &QAction::triggered, this, &MainWindow::goDirForward);
}

