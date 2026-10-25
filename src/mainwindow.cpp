#include "mainwindow_p.h"

#include "ShortcutHelper.h"

#include "core/image/decoder/RawDecodePlan.h"
#include "naturalnameorder.h"
#include "runtime_storage.h"
#include <QCoreApplication>

namespace
{
QDialog *createFullscreenCompareHost(QWidget *parent, CompareWorkspace **viewOut)
{
    // Qt::Window keeps MainWindow as QObject parent but is a top-level native
    // window. A child QDialog's showFullScreen() can leave a strip of the parent
    // visible on one edge.
    auto *dlg = new QDialog(parent, Qt::Window);
    dlg->setObjectName("compareDialog");
    dlg->setWindowTitle(QCoreApplication::translate("MainWindow", "Compare Mode - MViewer"));
    auto *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *view = new CompareWorkspace(dlg);
    layout->addWidget(view);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setProperty("mviewerFullscreenRequested", true);
    dlg->setWindowState(dlg->windowState() | Qt::WindowFullScreen);
    *viewOut = view;
    return dlg;
}

void writeCurrentSidecarAsync(MainWindow *window, const QString &path)
{
    if (!window || path.isEmpty())
        return;
    QPointer<MainWindow> alive(window);
    mviewer::core::SidecarStore::instance().writeSidecarAsync(
        path.toUtf8().toStdString(),
        [alive](const std::string &failedPath)
        {
            if (!alive)
                return;
            const QString name = QFileInfo(QString::fromStdString(failedPath)).fileName();
            alive->statusBar()->showMessage(
                MainWindow::tr("Cannot write the sidecar file for %1").arg(name), 4000);
        });
}

void seedCompareFromViewer(CompareWorkspace *compare, ImageViewer *viewer, const QStringList &imgs)
{
    if (!compare || !viewer)
        return;
    const QString viewerPath = viewer->currentPath();
    bool viewerListed = false;
    for (const QString &image : imgs)
        viewerListed = viewerListed || ImageViewer::browsePathEquals(image, viewerPath);
    if (!viewerPath.isEmpty() && viewerListed && viewer->isLodDisplay())
    {
        const QImage warm = viewer->displayRaster(); // shared bits; no deep copy
        if (!warm.isNull())
        {
            QSize srcSize;
            if (const auto frame = viewer->frame())
                srcSize = QSize(frame->metadata().width, frame->metadata().height);
            if (!srcSize.isValid())
                srcSize = warm.size();
            compare->seedWarmDisplay(viewerPath, warm, srcSize);
        }
    }
    // Neighbor warm rasters: move ownership into Compare to avoid re-decode.
    for (const QString &path : imgs)
    {
        if (path.isEmpty() || ImageViewer::browsePathEquals(path, viewerPath))
            continue;
        QImage image;
        QSize srcSize;
        QRect srcRect;
        if (!viewer->takeWarmDisplayForCompare(path, &image, &srcSize, &srcRect))
            continue;
        compare->seedWarmDisplay(path, image, srcSize, srcRect);
    }
}
} // namespace

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
    m_secondaryBrowseHistory.restore(m_appState.secondaryDirHistory,
                                     m_appState.secondaryDirHistoryIndex);
    m_directory->setFavorites(m_appState.favorites);
    m_directory->setRecentFolders(m_appState.recentFolders);
    m_workspace->setAnalysisVisible(m_appState.analysisVisible);
    m_workspace->setAnalysisPage(m_appState.analysisPage);
    const QString recentPath = mviewer::runtime::filePath(QStandardPaths::AppConfigLocation,
                                                          QStringLiteral("recent.json"));
    {
        QFile rf(recentPath);
        if (rf.open(QIODevice::ReadOnly))
        {
            // Recent-folders LRU is a small JSON array; bound the read.
            constexpr qint64 kMaxRecentFileBytes = 16LL * 1024 * 1024;
            if (rf.size() <= kMaxRecentFileBytes)
            {
                const QByteArray raw = rf.read(std::min<qint64>(rf.size(), kMaxRecentFileBytes));
                m_recent.deserialize(std::string(raw.constData(), raw.size()));
            }
        }
    }

    setupUi();
    setupCommands();
    setWindowTitle(tr("MViewer"));
    resize(1280, 800);
    setMinimumSize(800, 500); // prevent layout collapse at tiny sizes

    restoreWindowSettings();

    // P0: restore last folder + image + scroll position (deferred to event loop).
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    rebuildRecentFilesMenu();
    {
        QSettings settings;
        const int rawPolicy = settings.value(QStringLiteral("raw/displayPolicy"), 0).toInt();
        mviewer::core::setRawDisplayPolicy(mviewer::core::rawDisplayPolicyFromInt(rawPolicy));
    }
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
                           // after recovery has settled, avoiding nested modal activation locks.
                           QTimer::singleShot(500, this, &MainWindow::maybeShowCrashReport);
                       });
    // M17: quiet background update check shortly after launch (only notifies on a new version).
    // Acceptance harnesses can disable this unrelated network activity so
    // teardown remains deterministic; Preferences → autoCheckUpdates (default true)
    // lets users turn off the startup nag without blocking Help → Check for Updates.
    {
        QSettings settings;
        const bool autoCheck = settings.value(QStringLiteral("autoCheckUpdates"), true).toBool();
        if (autoCheck && !qEnvironmentVariableIsSet("MVIEWER_DISABLE_UPDATE_CHECK"))
            QTimer::singleShot(8000, this, [this]() { checkForUpdates(true); });
    }
    cleanupClipboardPasteTemps();
}

void MainWindow::restoreWindowSettings()
{
    QSettings settings;
    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("windowState"))
        restoreState(settings.value("windowState").toByteArray());
    // restoreState can reapply fullscreen after the old on-screen check.
    ImageViewer::clampWidgetToAvailableScreens(this);
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
    {
        m_thumbnailPanel->setViewMode(
            ThumbnailPanel::viewModeFromInt(settings.value("thumbViewMode").toInt()));
    }
    if (m_sortCombo && settings.contains("thumbSortMode"))
    {
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
    if (settings.contains("thumbSortAscending"))
    {
        const bool ascending = settings.value("thumbSortAscending", true).toBool();
        if (m_thumbnailPanel)
            m_thumbnailPanel->setSortAscending(ascending);
        if (m_sortDirBtn)
        {
            m_sortDirBtn->setChecked(!ascending);
            m_sortDirBtn->setText(ascending ? "↑" : "↓");
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
    {
        QFile::remove(m_clipboardPastePath);
        m_clipboardPastePath.clear();
    }
    cleanupClipboardPasteTemps();
    // ~QWidget deletes the children only after this body returns, when the
    // dynamic type is already QWidget. Any signal that then reaches a
    // MainWindow member-function slot downcasts a non-MainWindow object
    // (UBSan "object is of type 'QWidget'", Qt assert "class destructor may
    // have already run" in browse/paste/split acceptance, teardown stress and
    // m47 restore). The trigger is QApplication::focusChanged ->
    // onBrowseFocusChanged (qApp outlives the window and focus moves while the
    // children die); child senders (combo boxes, timers, ThumbnailPanel
    // selection/model signals) can emit on the way down too. Cut every
    // connection into this object last, after the teardown above that may
    // still rely on them.
    if (QCoreApplication *app = QCoreApplication::instance(); app != nullptr)
        QObject::disconnect(app, nullptr, this, nullptr);
    const auto owned = findChildren<QObject *>();
    for (QObject *child : owned)
        QObject::disconnect(child, nullptr, this, nullptr);
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
    // trigger repeated (expensive) index rebuilds. Cancel the previous
    // folder's search request now; the gallery filter keeps its own id.
    ++m_reindexGen;
    if (m_reindexAlive)
        m_reindexAlive->store(false, std::memory_order_release);
    if (m_reindexRequestId != 0)
    {
        mviewer::core::MetadataIndexer::instance().cancelRequest(m_reindexRequestId);
        m_reindexRequestId = 0;
    }
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
                        const auto e = mviewer::core::MetadataIndexer::instance().cached(
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
    mviewer::core::RatingStore::instance().setRating(currentImagePath().toUtf8().toStdString(),
                                                     stars);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath()); // refresh the rating widget
    writeCurrentSidecarAsync(this, currentImagePath());
    statusBar()->showMessage(
        tr("Rated %1: %2 stars").arg(QFileInfo(currentImagePath()).fileName()).arg(stars), 3000);
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
    // invalidateRatings() re-applies the active filter (single pipeline).
    m_thumbnailPanel->invalidateRatings();
}

void MainWindow::setCurrentColorLabel(int label)
{
    if (currentImagePath().isEmpty())
        return;
    mviewer::core::RatingStore::instance().setColorLabel(currentImagePath().toUtf8().toStdString(),
                                                         label);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    writeCurrentSidecarAsync(this, currentImagePath());
    const QString name = QFileInfo(currentImagePath()).fileName();
    statusBar()->showMessage(label == 0 ? tr("Cleared the color label of %1").arg(name)
                                        : tr("Set color label %2 on %1").arg(name).arg(label),
                             3000);
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
    writeCurrentSidecarAsync(this, currentImagePath());
    statusBar()->showMessage(v ? tr("Picked %1").arg(QFileInfo(currentImagePath()).fileName())
                               : tr("Unpicked %1").arg(QFileInfo(currentImagePath()).fileName()),
                             3000);
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
    writeCurrentSidecarAsync(this, currentImagePath());
    statusBar()->showMessage(v ? tr("Rejected %1").arg(QFileInfo(currentImagePath()).fileName())
                               : tr("Unrejected %1").arg(QFileInfo(currentImagePath()).fileName()),
                             3000);
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
    onImageOpenFromPanel(path, qobject_cast<ThumbnailPanel *>(sender()));
}

void MainWindow::onCurrentImageChanged(const QString &path)
{
    // P0-2 / M19: the ONE place that fans the current-image change out to every
    // view. SelectionModel already holds the path — do not re-set it here.
    if (path.isEmpty())
        return;

    ThumbnailPanel *sourcePanel = thumbnailPanelForPath(path, activeThumbnailPanel());
    const QPixmap warmThumbnail = sourcePanel ? sourcePanel->thumbReady(path) : QPixmap();
    QSize knownSourceSize;
    qint64 knownFileSize = -1;
    if (sourcePanel)
    {
        if (const auto *entry = sourcePanel->entryForPath(path))
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
    // short-circuits an already-selected directory in memory. A new folder is
    // confirmed off the GUI thread so a stale volume cannot freeze selection.
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
    if (m_lblRawSource)
        m_lblRawSource->clear(), m_lblRawSource->hide();
    statusBar()->showMessage(tr("Current: %1").arg(fileName), 3000);
    applyActivePaneStatusBar();

    ++m_statusMetadataGeneration;
    const uint64_t generation = m_statusMetadataGeneration;
    const QString requestedPath = path;
    QPointer<MainWindow> guard(this);
    mviewer::core::MetadataPresentationService::instance().request(
        path.toUtf8().toStdString(), m_statusMetadataConsumer,
        [guard, requestedPath,
         generation](const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
        {
            if (!guard)
                return;
            // Explicit hop: the service defers to the process main-thread
            // dispatcher, but the status strip must stay correct without one.
            QMetaObject::invokeMethod(
                qApp,
                [guard, requestedPath, generation, snapshot]()
                {
                    if (!guard || generation != guard->m_statusMetadataGeneration ||
                        !guard->m_selection || guard->m_selection->currentImage() != requestedPath)
                        return;
                    const auto &meta = snapshot.metadata;
                    if (meta.width > 0 && meta.height > 0)
                    {
                        const QString sizeStr = MainWindow::formatBytes(meta.fileSize);
                        if (!meta.format.empty())
                        {
                            const QString fmt = QString::fromStdString(meta.format).toUpper();
                            guard->m_lblImage->setText(QString("%1x%2 · %3 · %4")
                                                           .arg(meta.width)
                                                           .arg(meta.height)
                                                           .arg(fmt)
                                                           .arg(sizeStr));
                        }
                        else
                        {
                            guard->m_lblImage->setText(QString("%1x%2 · %3")
                                                           .arg(meta.width)
                                                           .arg(meta.height)
                                                           .arg(sizeStr));
                        }
                    }
                },
                Qt::QueuedConnection);
        });
}

void MainWindow::openDirectory(const QString &dir)
{
    changeDirectory(dir);
}

void MainWindow::changeDirectory(const QString &dir)
{
    if (dir.isEmpty())
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
    // didn't pass an explicit list (e.g. the "Compare Mode" menu).
    if (imgs.isEmpty())
        imgs = resolveSelectedPaths(true);
    // Default pane order is natural file-name order (img2 before img10), the
    // same comparator as the gallery's Name sort. The raw selection order is
    // whatever order Ctrl/Shift/rubber-band picks produced. A restored session
    // keeps its saved order because per-pane state is stored by index.
    if (sessionJson.isEmpty())
        imgs = sortPathsForCompare(imgs);
    // Compare is a 2-8 image selection. Do not substitute the first 8 files
    // of the folder when the user has 0-1 images selected.
    if (imgs.size() > 8)
    {
        imgs = imgs.mid(0, 8);
        statusBar()->showMessage(tr("At most 8 images can be compared; the first 8 were used"),
                                 5000);
    }
    if (imgs.size() < 2)
    {
        statusBar()->showMessage(tr("Select 2-8 images to compare"), 5000);
        return;
    }
    showCompareDialog(imgs, sessionJson);
}

void MainWindow::showCompareDialog(const QStringList &imgs, const QString &sessionJson)
{
    CompareWorkspace *view = nullptr;
    auto *dlg = createFullscreenCompareHost(this, &view);
    m_compareHost = dlg;
    m_compareView = view;
    QPointer<CompareWorkspace> viewGuard(m_compareView);
    // QWidget (not QDialog): QPointer<QDialog>::data() does a checked downcast
    // that fails under GCC ASan while QDialog is mid-destroy. Timer lambda below
    // still needs a live-guard for the post-show setImages shot.
    QPointer<QWidget> dialogGuard(dlg);
    // P0: Inject SelectionModel so CompareWorkspace writes focus back to global SSOT.
    // M28 P1-01: setImages() is async and is invoked once below (after the
    // dialog is shown and laid out) — the old pre-show call decoded every
    // image synchronously on the UI thread AND duplicated the load.
    m_compareView->setSelectionModel(m_selection);
    // A-4.5 / M19: continuous compare — seed the pool from ImageListModel.
    ensureImageList();
    if (m_imageList && !m_imageList->isEmpty())
        m_compareView->setImagePool(m_imageList->paths(), imgs);
    else if (imgs.size() >= 2)
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
                    statusBar()->showMessage(tr("Current: %1").arg(QFileInfo(current).fileName()),
                                             3000);
                else if (m_imageList && m_imageList->count() > 0)
                    statusBar()->showMessage(
                        QStringLiteral("Loaded %1 images").arg(m_imageList->count()), 2500);
                else
                    statusBar()->showMessage(tr("Ready"), 2000);
            });
    // P1 #④: Compare → Analyze workflow (Analyze button in Compare toolbar).
    connect(
        m_compareView, &CompareWorkspace::analyzeCurrent, this,
        [this, viewGuard]()
        {
            // Compare is a fullscreen host. Opening MainWindow's analysis panel
            // behind it looks like a no-op; the Compare inspector is the target.
            if (m_compareHost && m_compareHost->isVisible())
                return;
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

    // Capture as QObject*: QPointer<QDialog>::data() does a checked downcast
    // that fails (and ASan warns) while QDialog is mid-destroy.
    connect(dlg, &QObject::destroyed, this,
            [this, hostId = static_cast<QObject *>(dlg)]()
            {
                if (static_cast<QObject *>(m_compareHost.data()) == hostId)
                {
                    m_compareView = nullptr;
                    m_compareHost = nullptr;
                }
            });

    dlg->showFullScreen();

    seedCompareFromViewer(m_compareView, m_imageViewer, imgs);

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
                               session = decodeCompareSession(sessionFinal.toUtf8().toStdString());
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
