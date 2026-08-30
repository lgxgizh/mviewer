#include "mainwindow_p.h"

#include "runtime_storage.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    // Library/test hosts may construct MainWindow without going through the
    // production main(). Establish the same file-backed QSettings contract at
    // the UI boundary before any settings are read.
    mviewer::runtime::configureSettings();
    m_statusMetadataConsumer =
        "mainwindow-status-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    // M19: UI models — single source of truth for Current / Selection /
    // Directory / ImageList / Workspace / Analyzer. Every panel reacts to these
    // instead of tracking its own copy of the same state.
    m_selection = new SelectionModel(this);
    m_directory = new DirectoryModel(this);
    m_imageList = new ImageListModel(this);
    m_directoryMonitor = new DirectoryMonitor(this);
    m_workspace = new WorkspaceModel(this);
    m_analyzer = new AnalyzerModel(this);

    // P0: load persisted cross-session state + recent-folders LRU before UI.
    m_appState = AppState::load();
    m_directory->setFavorites(m_appState.favorites);
    m_directory->setRecentFolders(m_appState.recentFolders);
    m_workspace->setAnalysisVisible(m_appState.analysisVisible);
    m_workspace->setAnalysisPage(m_appState.analysisPage);
    const QString recentPath =
        mviewer::runtime::filePath(QStandardPaths::AppConfigLocation, QStringLiteral("recent.json"));
    {
        QFile rf(recentPath);
        if (rf.open(QIODevice::ReadOnly))
        {
            const QByteArray raw = rf.readAll();
            m_recent.deserialize(std::string(raw.constData(), raw.size()));
        }
    }

    setupUi();
    setupCommands();
    setWindowTitle("MViewer");
    resize(1280, 800);
    setMinimumSize(800, 500); // prevent layout collapse at tiny sizes

    restoreWindowSettings();

    // P0: restore last folder + image + scroll position (deferred to event loop).
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    rebuildRecentFilesMenu();
    restoreLastSession();

    // M15: drag & drop — accept files/folders dropped onto the window.
    setAcceptDrops(true);

    // Give the gallery keyboard focus on launch so arrow-key navigation works
    // immediately without the user having to click first.
    if (m_thumbnailPanel)
        m_thumbnailPanel->setFocus();

    // M15: crash recovery — autosave current session every 30s + restore on launch.
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveSession);
    m_autosaveTimer->start(30000);
    m_autosaveLoaded = false;
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           if (qEnvironmentVariableIsSet("MVIEWER_DISABLE_RECOVERY_PROMPTS"))
                               return;
                           restoreSessionRecovery();
                           // M17: if a previous run crashed, surface a crash-report prompt
                           // on next launch.
                           maybeShowCrashReport();
                       });
    // M17: quiet background update check shortly after launch (only notifies on a new version).
    // Acceptance harnesses can disable this unrelated network activity so
    // teardown remains deterministic; production keeps the normal check.
    if (!qEnvironmentVariableIsSet("MVIEWER_DISABLE_UPDATE_CHECK"))
        QTimer::singleShot(8000, this, [this]() { checkForUpdates(true); });
    cleanupClipboardPasteTemps();
}

void MainWindow::restoreWindowSettings()
{
    QSettings settings;
    if (settings.contains("geometry"))
    {
        restoreGeometry(settings.value("geometry").toByteArray());
        const QRect wr = frameGeometry();
        bool onAnyScreen = false;
        for (QScreen *screen : QGuiApplication::screens())
        {
            if (screen->availableGeometry().intersects(wr))
            {
                onAnyScreen = true;
                break;
            }
        }
        if (!onAnyScreen)
        {
            const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
            move(available.center() - QPoint(width() / 2, height() / 2));
        }
    }
    if (settings.contains("windowState"))
        restoreState(settings.value("windowState").toByteArray());
    if (m_mainSplitter && settings.contains("splitterState"))
        m_mainSplitter->restoreState(settings.value("splitterState").toByteArray());
    restoreNavigationSettings(settings);
}

void MainWindow::restoreNavigationSettings(const QSettings &settings)
{
    if (m_mainSplitter && settings.contains("navSidebarWidth"))
    {
        const int navWidth = settings.value("navSidebarWidth").toInt();
        if (navWidth > 40)
        {
            QList<int> sizes = m_mainSplitter->sizes();
            if (!sizes.isEmpty())
            {
                const int delta = navWidth - sizes[0];
                sizes[0] = navWidth;
                if (sizes.size() > 1)
                    sizes[1] = qMax(100, sizes[1] - delta);
                m_mainSplitter->setSizes(sizes);
            }
        }
    }
    if (m_leftSplitter && settings.contains("leftSplitterState"))
        m_leftSplitter->restoreState(settings.value("leftSplitterState").toByteArray());
    if (m_thumbnailPanel && settings.contains("thumbViewMode"))
        m_thumbnailPanel->setViewMode(
            static_cast<ThumbnailPanel::ViewMode>(settings.value("thumbViewMode").toInt()));
    if (!m_sortCombo || !settings.contains("thumbSortMode"))
        return;
    const int sortMode = settings.value("thumbSortMode").toInt();
    for (int i = 0; i < m_sortCombo->count(); ++i)
    {
        if (m_sortCombo->itemData(i).toInt() == sortMode)
        {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
}

MainWindow::~MainWindow()
{
    cancelClipboardPaste();
    cancelReportExport();
    cancelBackgroundPersistence();
    // M47: cancel an in-flight workspace/project restore; the delivery is
    // additionally QPointer- and generation-guarded, so a late completion can
    // never touch freed MainWindow state.
    TaskScheduler::cancel(m_restoreTask);
    m_restoreTask.reset();
    TaskScheduler::cancel(m_sidecarImportTask);
    m_sidecarImportTask.reset();
    if (m_sidecarImportAlive)
        m_sidecarImportAlive->store(false, std::memory_order_release);
    // M27 lifetime closure: stop the async search re-index immediately. The
    // worker callback checks the alive token before marshaling, and the queued
    // UI lambda checks it again before touching any member — without this, a
    // re-index that completes after the window is destroyed dereferences freed
    // MainWindow state (the alive token also gates the MetadataIndexer's
    // worker-side deliveries for this request).
    if (m_reindexAlive)
        *m_reindexAlive = false;
    if (m_reindexRequestId != 0)
        mviewer::core::MetadataIndexer::instance().cancelRequest(m_reindexRequestId);
    // P0-3: cancel the in-flight metadata-overlay histogram and invalidate its
    // generation. The queued delivery is additionally QPointer-guarded, so a
    // late completion can never touch freed MainWindow/overlay state.
    cancelMetadataHistogram();
    mviewer::core::MetadataPresentationService::instance().cancel(m_statusMetadataConsumer);
    // M27: the ImageViewer is a parentless top-level window (mainwindow_ui.cpp
    // creates it with nullptr) — nothing owns it, so it must be deleted here.
    // Without this, every MainWindow create/destroy leaks the viewer window
    // (GL surface, backing store, tile cache, decoded pixmap) — measured at
    // ~100 MB RSS + ~50 OS handles per lifecycle in the close/shutdown
    // torture. The viewer's async deliveries are QPointer-guarded, so deleting
    // it while a decode is in flight is safe.
    delete m_imageViewer;
    m_imageViewer = nullptr;
    if (!m_clipboardPastePath.isEmpty())
        QFile::remove(m_clipboardPastePath);
    cleanupClipboardPasteTemps();
}

void MainWindow::onSearchMetaToggled(bool on)
{
    m_thumbnailPanel->setMetaSearch(on);
}

void MainWindow::scheduleReindex()
{
    if (!m_searchPanel)
        return;
    if (!m_reindexTimer)
    {
        m_reindexTimer = new QTimer(this);
        m_reindexTimer->setSingleShot(true);
        m_reindexTimer->setInterval(500);
        connect(m_reindexTimer, &QTimer::timeout, this, &MainWindow::reindexSearch);
    }
    // Restart the countdown on every folder change so rapid browsing does not
    // trigger repeated (expensive) index rebuilds.
    m_reindexTimer->start();
}

void MainWindow::reindexSearch()
{
    if (!m_searchPanel)
        return;

    // M25: the search index is built OFF the UI thread by the shared
    // MetadataIndexer (the same index the gallery filters reuse). Before this,
    // the whole directory's metadata was read + parsed synchronously on the
    // UI thread on every folder change — a multi-second freeze on 10k folders.
    std::vector<std::string> paths;
    const QStringList listPaths = m_imageList->paths();
    paths.reserve(static_cast<size_t>(listPaths.size()));
    for (const QString &p : listPaths)
        paths.push_back(p.toUtf8().toStdString());

    ++m_reindexGen;
    const uint64_t gen = m_reindexGen;
    // M27: the alive token is a member so the destructor can flip it — a local
    // token would keep the queued lambda alive past destruction.
    auto alive = m_reindexAlive = std::make_shared<std::atomic<bool>>(true);
    // M26: requests are per-consumer — supersede ONLY our own stale request
    // (never another consumer's in-flight index, e.g. the gallery's metadata
    // filter). A rejected submission (0) means no callbacks will arrive.
    if (m_reindexRequestId != 0)
        mviewer::core::MetadataIndexer::instance().cancelRequest(m_reindexRequestId);
    const uint64_t requestId = mviewer::core::MetadataIndexer::instance().index(
        paths, [](const mviewer::core::MetadataIndexer::Entry &) {},
        [this, alive, gen]()
        {
            if (!alive->load())
                return;
            QMetaObject::invokeMethod(
                qApp,
                [this, alive, gen]()
                {
                    if (!alive->load() || gen != m_reindexGen || !m_searchPanel)
                        return;
                    // Collect the freshly indexed entries (no I/O — cached).
                    std::vector<mviewer::core::MetadataIndexEntry> entries;
                    const QStringList cur = m_imageList->paths();
                    entries.reserve(static_cast<size_t>(cur.size()));
                    for (const QString &p : cur)
                    {
                        const auto e =
                            mviewer::core::MetadataIndexer::instance().cached(
                                p.toUtf8().toStdString());
                        if (e)
                            entries.push_back(*e);
                    }
                    m_searchPanel->reindexEntries(entries);
                });
        });
    m_reindexRequestId = requestId;
}

void MainWindow::onRatingFilterChanged(int)
{
    m_thumbnailPanel->setRatingFilter(m_ratingFilter->currentData().toInt());
}

void MainWindow::rateCurrentImage(int stars)
{
    if (currentImagePath().isEmpty())
        return;
    mviewer::core::RatingStore::instance().setRating(currentImagePath().toUtf8().toStdString(), stars);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath()); // refresh the rating widget
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toUtf8().toStdString());
    statusBar()->showMessage(
        QString("已为 %1 评分: %2 星").arg(QFileInfo(currentImagePath()).fileName()).arg(stars));
}

void MainWindow::onFlagFilterChanged(int)
{
    const int v = m_flagFilter->currentData().toInt();
    m_thumbnailPanel->clearFlagFilters();
    m_thumbnailPanel->setRatingFilter(m_ratingFilter->currentData().toInt());
    switch (v)
    {
    case 1:
        m_thumbnailPanel->setPickFilter(true);
        break;
    case 2:
        m_thumbnailPanel->setRejectFilter(true);
        break;
    case 3:
        m_thumbnailPanel->setRecentFilter(true);
        break;
    case 11:
        m_thumbnailPanel->setLabelFilter(1);
        break;
    case 12:
        m_thumbnailPanel->setLabelFilter(2);
        break;
    case 13:
        m_thumbnailPanel->setLabelFilter(3);
        break;
    case 14:
        m_thumbnailPanel->setLabelFilter(4);
        break;
    case 15:
        m_thumbnailPanel->setLabelFilter(5);
        break;
    case 16:
        m_thumbnailPanel->setLabelFilter(6);
        break;
    default:
        break;
    }
}

void MainWindow::onFlagsEdited(const QString &path, int label, bool rejected, bool picked)
{
    Q_UNUSED(path);
    Q_UNUSED(label);
    Q_UNUSED(rejected);
    Q_UNUSED(picked);
    m_thumbnailPanel->invalidateRatings();
    // Re-apply the active filter so gallery membership stays correct.
    m_thumbnailPanel->setRatingFilter(m_ratingFilter->currentData().toInt());
}

void MainWindow::setCurrentColorLabel(int label)
{
    if (currentImagePath().isEmpty())
        return;
    mviewer::core::RatingStore::instance().setColorLabel(currentImagePath().toUtf8().toStdString(), label);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toUtf8().toStdString());
    const QString name = QFileInfo(currentImagePath()).fileName();
    statusBar()->showMessage(label == 0 ? QString("已清除 %1 的色标").arg(name)
                                        : QString("已为 %1 设置色标 %2").arg(name).arg(label));
}

void MainWindow::toggleCurrentPick()
{
    if (currentImagePath().isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.picked(currentImagePath().toUtf8().toStdString());
    rs.setPicked(currentImagePath().toUtf8().toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toUtf8().toStdString());
    statusBar()->showMessage(
        v ? QString("已收藏 %1").arg(QFileInfo(currentImagePath()).fileName())
          : QString("已取消收藏 %1").arg(QFileInfo(currentImagePath()).fileName()));
}

void MainWindow::toggleCurrentReject()
{
    if (currentImagePath().isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.rejected(currentImagePath().toUtf8().toStdString());
    rs.setRejected(currentImagePath().toUtf8().toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toUtf8().toStdString());
    statusBar()->showMessage(
        v ? QString("已拒绝 %1").arg(QFileInfo(currentImagePath()).fileName())
          : QString("已取消拒绝 %1").arg(QFileInfo(currentImagePath()).fileName()));
}

void MainWindow::setOpenOnLaunch(const QString &path)
{
    if (path.isEmpty())
        return;

    setOpenOnLaunch(QStringList{path});
}

void MainWindow::setOpenOnLaunch(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    m_openOnLaunch = paths;
    if (m_openOnLaunchQueued)
        return;

    m_openOnLaunchQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            m_openOnLaunchQueued = false;
            const QStringList paths = m_openOnLaunch;
            m_openOnLaunch.clear();
            if (!paths.isEmpty())
                openExternalTargets(paths);
        },
        Qt::QueuedConnection);
}

void MainWindow::onImageOpen(const QString &path)
{
    const bool wasHidden = m_imageViewer->isHidden();
    // P0-2: route the "current image" change through the shared model so every
    // panel (preview, metadata, status bar, thumbnail highlight) syncs centrally
    // in onCurrentImageChanged(). The central handler only decodes into the
    // viewer when it is already visible, so when opening it fresh we set the
    // image explicitly below.
    m_selection->setCurrentImage(path);
    if (wasHidden)
    {
        // Gallery double-clicks already belong to the published sequence. A
        // direct recent-file open may not, so keep that standalone viewer
        // request navigable without touching the Browse SSOT or scanning disk.
        QStringList sequence = m_imageList ? m_imageList->paths() : QStringList();
        if (!sequence.contains(path))
            sequence = {path};
        m_imageViewer->setBrowseSequence(sequence);
        if (m_thumbnailPanel)
        {
            const QPixmap warm = m_thumbnailPanel->thumbReady(path);
            QSize knownSize;
            if (const auto *entry = m_thumbnailPanel->entryForPath(path))
                knownSize = QSize(entry->width, entry->height);
            if (!warm.isNull())
                m_imageViewer->setProvisionalImage(path, warm.toImage(), knownSize);
        }
        m_imageViewer->setImage(path); // async; imageReady() feeds AnalysisPanel
    }

    // --- "open" extras (only meaningful for an explicit open, not selection) ---
    pushHistory(path); // P0: in-session browse history
    // P0-1: cross-session image history.
    m_appState.addHistory(path);
    // M14-1: track in recent-files LRU + refresh menu.
    m_recentFiles.add(path.toUtf8().toStdString());
    rebuildRecentFilesMenu();
    // M12.2 / M19: restore saved analysis from AnalyzerModel if present.
    const QString savedAnalysis = m_analyzer->resultText(path);
    if (!savedAnalysis.isEmpty())
        m_analysisPanel->setRegionStats(savedAnalysis);
    if (wasHidden)
        m_imageViewer->showBrowseFullscreen();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
}

void MainWindow::onCurrentImageChanged(const QString &path)
{
    // P0-2 / M19: the ONE place that fans the current-image change out to every
    // view. SelectionModel already holds the path — do not re-set it here.
    if (path.isEmpty())
        return;

    const QPixmap warmThumbnail = m_thumbnailPanel ? m_thumbnailPanel->thumbReady(path) : QPixmap();
    QSize knownSourceSize;
    qint64 knownFileSize = -1;
    if (m_thumbnailPanel)
    {
        if (const auto *entry = m_thumbnailPanel->entryForPath(path))
        {
            knownSourceSize = QSize(entry->width, entry->height);
            knownFileSize = entry->size;
        }
    }
    m_previewPanel->setImage(path, warmThumbnail, knownSourceSize, knownFileSize);
    // Only decode into the viewer when it is actually on screen — avoids a
    // second decode per thumbnail while browsing with the viewer closed.
    if (!m_imageViewer->isHidden())
    {
        if (!warmThumbnail.isNull())
            m_imageViewer->setProvisionalImage(path, warmThumbnail.toImage(), knownSourceSize);
        m_imageViewer->setImage(path);
    }

    // Metadata presentation is memory-first. Hidden consumers only record the
    // identity; visible consumers share one background single-flight request.
    if (m_metadataOverlay)
    {
        cancelMetadataHistogram();
        m_metadataOverlay->setImage(path);
        m_metadataOverlay->setHistogram(mviewer::core::Histogram());
        if (m_actToggleMetadata && m_actToggleMetadata->isChecked())
            m_metadataOverlay->showForImage(path);
    }
    if (m_metadataPanel && m_metadataPanel->isVisible())
        m_metadataPanel->setImage(path);

    // Sync the committed SelectionModel pair; m_syncingSelection blocks gallery feedback.
    syncGalleryFromSelection();

    // P0: Auto-locate the directory tree to the image's parent folder. The tree
    // itself short-circuits an already-selected directory before QFileInfo or
    // QFileSystemModel work, so rapid same-folder selection is memory-only.
    const int slash = path.lastIndexOf(QChar('/'));
    const int backslash = path.lastIndexOf(QChar('\\'));
    const int separator = qMax(slash, backslash);
    const QString dir = separator > 0 ? path.left(separator) : QString();
    if (m_directoryTree && !dir.isEmpty())
        m_directoryTree->navigateTo(dir);

    mviewer::core::RatingStore::instance().addRecent(path.toUtf8().toStdString()); // P3 recents

    // Window title + status bar identity follow the current image. Dimensions
    // arrive from the shared metadata presentation service; never re-read the
    // file synchronously from the selection signal handler.
    const QString fileName = separator >= 0 ? path.mid(separator + 1) : path;
    setWindowTitle(QString("%1 - MViewer").arg(fileName));
    m_lblImage->setText(fileName);
    statusBar()->showMessage(QString("当前: %1").arg(fileName));

    ++m_statusMetadataGeneration;
    const uint64_t generation = m_statusMetadataGeneration;
    const QString requestedPath = path;
    QPointer<MainWindow> guard(this);
    mviewer::core::MetadataPresentationService::instance().request(
        path.toUtf8().toStdString(), m_statusMetadataConsumer,
        [guard, requestedPath, generation](
            const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
        {
            if (!guard || generation != guard->m_statusMetadataGeneration ||
                !guard->m_selection || guard->m_selection->currentImage() != requestedPath)
                return;
            const auto &meta = snapshot.metadata;
            if (meta.width > 0 && meta.height > 0)
            {
                guard->m_lblImage->setText(
                    QString("%1x%2 · %3")
                        .arg(meta.width)
                        .arg(meta.height)
                        .arg(MainWindow::formatBytes(meta.fileSize)));
            }
        });
}

void MainWindow::openDirectory(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return;
    m_directoryTree->navigateTo(dir);
}

void MainWindow::changeDirectory(const QString &dir)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return;

    // DirectoryTree::directoryChanged is the committed-transition boundary.
    // All directory-scoped side effects live in its MainWindow owner below so
    // every entry point (tree, breadcrumb, history, path edit, and restore)
    // follows exactly the same path.
    m_directoryTree->navigateTo(dir, true);
}

void MainWindow::openCompare(const QStringList &images, const QString &sessionJson)
{
    // Product contract: one Compare host per MainWindow. Close the old host
    // before creating the replacement so no global pointer can represent two
    // sessions at once. Queued work below is guarded by the concrete dialog and
    // workspace it belongs to, not by m_compareView.
    if (m_compareHost)
    {
        m_compareHost->close();
        m_compareHost = nullptr;
        m_compareView = nullptr;
    }
    QStringList imgs = images;
    // A-3: prefer the shared SelectionModel multi-selection when the caller
    // didn't pass an explicit list (e.g. menu "比较模式").
    if (imgs.isEmpty())
        imgs = resolveSelectedPaths(true);
    // Compare needs ≥2 images; if only one is selected, fall back to the folder.
    if (imgs.size() < 2)
    {
        ensureImageList();
        imgs = m_imageList ? m_imageList->paths() : QStringList();
    }
    // Compare sessions are documented for 2-8 images: trim oversized
    // fallbacks to the supported range and refuse to open a degenerate
    // single-image “compare” (with user feedback instead of a silent no-op
    // or a one-pane dialog).
    if (imgs.size() > 8)
    {
        imgs = imgs.mid(0, 8);
        statusBar()->showMessage(tr("最多支持 8 张图片对比，已使用前 8 张"), 5000);
    }
    if (imgs.size() < 2)
    {
        statusBar()->showMessage(tr("需要至少两张图片才能比较"), 5000);
        return;
    }
    showCompareDialog(imgs, sessionJson);
}

void MainWindow::showCompareDialog(const QStringList &imgs, const QString &sessionJson)
{
    auto *dlg = new QDialog(this);
    m_compareHost = dlg;
    dlg->setObjectName("compareDialog");
    dlg->setWindowTitle("比较模式 - MViewer");
    auto *layout = new QVBoxLayout(dlg);
    m_compareView = new CompareWorkspace(dlg);
    QPointer<CompareWorkspace> viewGuard(m_compareView);
    QPointer<QDialog> dialogGuard(dlg);
    layout->addWidget(m_compareView);
    // P0: Inject SelectionModel so CompareWorkspace writes focus back to global SSOT.
    // M28 P1-01: setImages() is async and is invoked once below (after the
    // dialog is shown and laid out) — the old pre-show call decoded every
    // image synchronously on the UI thread AND duplicated the load.
    m_compareView->setSelectionModel(m_selection);
    // A-4.5 / M19: continuous compare — seed the pool from ImageListModel.
    ensureImageList();
    if (m_imageList && !m_imageList->isEmpty())
        m_compareView->setImagePool(m_imageList->paths(), imgs);
    else if (imgs.size() > 2)
        m_compareView->setImagePool(imgs, imgs);
    // M19: WorkspaceModel tracks the live compare set.
    if (m_workspace)
        m_workspace->setComparedImages(imgs);
    connect(m_compareView, &CompareWorkspace::pixelInfo, this,
            [this](const QString &text) { statusBar()->showMessage(text); });
    // M24 (B#7): failed compare loads surface in the status bar (non-modal).
    connect(m_compareView, &CompareWorkspace::loadWarning, this,
            [this](const QString &text) { statusBar()->showMessage(text, 10000); });

    connect(dlg, &QDialog::finished, this,
            [this, viewGuard](int)
            {
                // WA_DeleteOnClose schedules destruction after finished(). Stop
                // late Compare deliveries before rebuilding Browse's message
                // from its current SSOT. The workspace remains alive until the
                // dialog's deferred deletion.
                if (viewGuard)
                    QObject::disconnect(viewGuard.data(), nullptr, this, nullptr);
                const QString current = currentImagePath();
                if (!current.isEmpty())
                    statusBar()->showMessage(
                        QString("当前: %1").arg(QFileInfo(current).fileName()));
                else if (m_imageList && !m_imageList->directory().isEmpty())
                    statusBar()->showMessage(
                        QStringLiteral("Browse: %1, %2 images")
                            .arg(m_imageList->directory())
                            .arg(m_imageList->count()));
                else
                    statusBar()->showMessage(tr("就绪"));
            });
    // P1 #④: Compare → Analyze workflow (Analyze button in Compare toolbar).
    connect(
        m_compareView, &CompareWorkspace::analyzeCurrent, this,
        [this, viewGuard]()
        {
            if (viewGuard)
            {
                const QString path = viewGuard->focusImagePath();
                if (!path.isEmpty())
                {
                    m_selection->setCurrentImage(path);
                    m_imageViewer->setImage(
                        path); // imageReady -> AnalysisPanel::setFrame (async, visibility-gated)
                }
            }
            if (m_analysisPanel && !m_analysisPanel->isVisible())
                m_analysisPanel->show();
        });
    // P1 #④: Compare → Export Report workflow (Export button in Compare toolbar).
    connect(m_compareView, &CompareWorkspace::exportReportRequested, this,
            [this]() { exportReport(); });

    connect(dlg, &QDialog::destroyed, this,
            [this, viewGuard, dialogGuard]()
            {
                if (m_compareHost == dialogGuard)
                {
                    m_compareView = nullptr;
                    m_compareHost = nullptr;
                }
            });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Set the state before show so the first native frame is fullscreen.
    // The property is the platform-independent contract for headless Qt,
    // whose offscreen plugin intentionally discards fullscreen window state.
    dlg->setProperty("mviewerFullscreenRequested", true);
    dlg->setWindowState(dlg->windowState() | Qt::WindowFullScreen);
    dlg->showFullScreen();

    // Load images *after* the dialog has been shown AND the event loop has
    // processed the layout pass, so that cell widgets have valid geometry when
    // fitAll() computes the shared zoom scale. Without this delay, cell
    // size() returns (0,0), fitAll() skips every cell, and the shared scale
    // stays at 1.0 — making large images render off-screen and the compare
    // view appears blank.
    const QStringList imgsFinal = imgs;
    const QString sessionFinal = sessionJson;
    QTimer::singleShot(0, dlg,
                       [dialogGuard, viewGuard, imgsFinal, sessionFinal]()
                       {
                           if (!dialogGuard || !viewGuard)
                               return;
                           std::optional<mviewer::domain::CompareSession> session;
                           if (!sessionFinal.isEmpty())
                               session = decodeCompareSession(
                                   sessionFinal.toUtf8().toStdString());
                           QVector<int> frameIndices;
                            if (session)
                                for (int i = 0; i < imgsFinal.size(); ++i)
                                    frameIndices.append(
                                        i < static_cast<int>(session->frameIndices.size())
                                            ? std::max(0, session->frameIndices[i])
                                            : 0);
                            viewGuard->setImages(imgsFinal, frameIndices);
                            if (session)
                                viewGuard->applySession(*session);
                        });
}
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
    // A-3.4: commands reflect whether their required source data is actually available.
    const int n = m_selection ? m_selection->selection().size() : 0;
    const int availableCount =
        std::max(m_imageList ? m_imageList->count() : 0,
                 m_thumbnailPanel ? static_cast<int>(m_thumbnailPanel->pathList().size()) : 0);
    const bool hasImages = availableCount > 0;
    const bool hasCurrent = !currentImagePath().isEmpty();
    if (m_actCompare)
        m_actCompare->setEnabled(n >= 2 || availableCount >= 2);
    if (m_actExportImages)
        m_actExportImages->setEnabled(n >= 1 || hasImages);
    if (m_actBatch)
        m_actBatch->setEnabled(n >= 1 || hasImages);
    if (m_actToggleMetadata)
    {
        m_actToggleMetadata->setEnabled(hasCurrent);
        if (!hasCurrent)
            m_actToggleMetadata->setChecked(false);
    }
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
    // M37: ThumbnailPanel is the only owner of directory scan/sort/filter.
    // This helper remains for callers that need to assert the active directory,
    // but it must never synchronously enumerate the filesystem on the UI thread.
    if (m_imageList->directory() != dir)
        m_imageList->setPaths({}, dir);
}

void MainWindow::syncGalleryFromSelection()
{
    if (!m_selection || !m_thumbnailPanel || m_syncingSelection)
        return;
    const QStringList sel = m_selection->selection();
    const QString cur = m_selection->currentImage();
    // Avoid feedback when the gallery already matches (common path: user clicked
    // a thumbnail → gallery selectionChanged → setSelection → here).
    const QStringList gallery = m_thumbnailPanel->selectedPaths();
    if (gallery == sel)
    {
        // Selection set matches — only move focus if needed (preserve multi).
        if (!cur.isEmpty())
            m_thumbnailPanel->selectPath(cur);
        return;
    }
    m_syncingSelection = true;
    if (sel.size() <= 1)
    {
        if (!cur.isEmpty())
            m_thumbnailPanel->selectPath(cur);
        else if (!sel.isEmpty())
            m_thumbnailPanel->selectPath(sel.first());
    }
    else
    {
        m_thumbnailPanel->selectPaths(sel, cur);
    }
    m_syncingSelection = false;
}
