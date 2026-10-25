// MainWindow signal wiring split by workflow responsibility.
#include "mainwindow_p.h"

#include <QDateTime>

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

void MainWindow::connectSelectionSignals()
{
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
                if (m_imageViewer && !m_imageViewer->isHidden())
                    m_imageViewer->setImage({});
                if (m_previewPanel)
                    m_previewPanel->setImage({});
                if (m_metadataPanel)
                    m_metadataPanel->setImage({});
                if (m_metadataOverlay)
                    m_metadataOverlay->hide();
                if (m_actToggleMetadata)
                    m_actToggleMetadata->setChecked(false);
                if (m_lblImage)
                    m_lblImage->setText(tr("No image selected"));
                const QString activeDir = m_directory ? m_directory->currentDirectory() : QString();
                setWindowTitle(activeDir.isEmpty()
                                   ? QStringLiteral("MViewer")
                                   : QString("%1 - MViewer").arg(QDir(activeDir).dirName()));
                updateSelectionActions();
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpenFromPanel(path, m_thumbnailPanel); });
    connect(m_thumbnailPanel, &ThumbnailPanel::compareRequested, this,
            [this](const QStringList &images) { openCompare(images); });
    // A-3 / M19: ThumbnailPanel is the sole gallery selection publisher. Its
    // selectionChanged handler owns the native ExtendedSelection gesture and
    // publishes the full ordered set to the app-wide SelectionModel.
    // A-3.4 / M19: SelectionModel → gallery (full multi-select, not just current).
    connect(m_selection, &SelectionModel::selectionChanged, this,
            [this](const QStringList &)
            {
                syncGalleryFromSelection();
                updateSelectionActions();
                updateSplitBrowseCompareButton();
            });
    // Dropping files directly onto the gallery behaves the same as dropping
    // them anywhere else on the window. Secondary pane wiring lives in
    // wireSecondaryBrowseHostSignals().
    connect(m_thumbnailPanel, &ThumbnailPanel::filesDropped, this, &MainWindow::handleDroppedPaths);
    // When the user deletes images from the gallery, advance the viewer off the
    // deleted image if it was the one being viewed, and drop the paths from
    // both panes.
    connect(m_thumbnailPanel, &ThumbnailPanel::pathsRemoved, this,
            [this](const QStringList &deleted)
            { onGalleryPathsRemoved(m_thumbnailPanel, deleted); });
    connect(m_thumbnailPanel, &ThumbnailPanel::pathsRenamed, this,
            &MainWindow::onGalleryPathsRenamed);
    connect(m_thumbnailPanel, &ThumbnailPanel::pathsModified, this,
            &MainWindow::onGalleryPathsModified);
}

static void updateViewerPixelStatus(QStatusBar *sb, int x, int y, int r, int g, int b, int a,
                                    int r16, int g16, int b16, int rawKind, bool valid)
{
    if (!sb || (sb->window() && !sb->window()->isVisible()))
        return;
    if (!valid)
    {
        // Leaving the image restores the previous message; it does not clear it.
        if (!sb->currentMessage().startsWith(QLatin1String("Pixel [")))
            return;
        const QString base = sb->property("statusBase").toString();
        const qint64 until = sb->property("statusBaseUntil").toLongLong();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (base.isEmpty() || (until > 0 && now >= until))
            sb->clearMessage();
        else if (until > 0)
        {
            const qint64 remain = until - now > 600000 ? 600000 : until - now;
            sb->showMessage(base, static_cast<int>(remain));
        }
        else
            sb->showMessage(base);
        return;
    }
    if (!sb->currentMessage().startsWith(QLatin1String("Pixel [")))
    {
        const QString current = sb->currentMessage();
        sb->setProperty("statusBase", current);
        const bool sticky = current.startsWith(QStringLiteral("Directory temporarily unavailable"));
        const qint64 untilMs =
            current.isEmpty() || sticky ? qint64{0} : QDateTime::currentMSecsSinceEpoch() + 4000;
        sb->setProperty("statusBaseUntil", untilMs);
    }
    const QString hex = QStringLiteral("#%1%2%3")
                            .arg(r, 2, 16, QChar('0'))
                            .arg(g, 2, 16, QChar('0'))
                            .arg(b, 2, 16, QChar('0'))
                            .toUpper();
    QString text = QStringLiteral("Pixel [%1,%2]").arg(x).arg(y);
    if (rawKind == 2)
        text += QStringLiteral("RGB(%1,%2,%3)  16bit(%4,%5,%6)  %7")
                    .arg(r)
                    .arg(g)
                    .arg(b)
                    .arg(r16)
                    .arg(g16)
                    .arg(b16)
                    .arg(hex);
    else if (rawKind == 1)
        text += QStringLiteral("RGB(%1,%2,%3)  (RAW)  %4").arg(r).arg(g).arg(b).arg(hex);
    else if (a < 255)
        text += QStringLiteral("RGBA(%1,%2,%3,%4)  %5").arg(r).arg(g).arg(b).arg(a).arg(hex);
    else
        text += QStringLiteral("RGB(%1,%2,%3)  %4").arg(r).arg(g).arg(b).arg(hex);
    sb->showMessage(text);
}

void MainWindow::connectViewerSignals()
{
    connect(m_imageViewer, &ImageViewer::regionStats, m_analysisPanel,
            &AnalysisPanel::setRegionStats);
    // P0 (cont.): feed the decoded ImageFrame to the analysis panel once the async
    // load completes (no re-decode on the UI thread). This replaces the old
    // synchronous QImage(path) decode that blocked browsing.
    connect(m_imageViewer, &ImageViewer::imageReady, m_analysisPanel, &AnalysisPanel::setFrame);
    connect(m_imageViewer, &ImageViewer::imageReady, m_metadataPanel, &MetadataPanel::setFrame);
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
                // An empty selection is propagated too, so the panel falls back
                // to whole-image statistics instead of keeping a stale ROI.
                mviewer::domain::Selection roi;
                roi.x = sel.x();
                roi.y = sel.y();
                roi.width = sel.width();
                roi.height = sel.height();
                m_analysisPanel->setROI(roi);
            });
    connect(m_imageViewer, &ImageViewer::requestPrev, this, [this]() { navigate(-1); });
    connect(m_imageViewer, &ImageViewer::requestNext, this, [this]() { navigate(1); });
    connect(m_imageViewer, &ImageViewer::requestDirBack, this, &MainWindow::goDirBack);
    connect(m_imageViewer, &ImageViewer::requestDirForward, this, &MainWindow::goDirForward);
    connect(m_imageViewer, &ImageViewer::viewerClosed, this,
            [this]()
            {
                // Defer state refresh so menu zoom actions observe the final hidden state.
                QTimer::singleShot(0, this, &MainWindow::updateSelectionActions);
                if (!isVisible())
                    return;
                raise();
                activateWindow();
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setFocus(Qt::OtherFocusReason);
            });
    // A-7.3: viewer context-menu "Analyze" → show panel + run through unified entry.
    // A fullscreen viewer covers the main window, so the panel would open
    // behind it. Leave fullscreen on the viewer's own path, then raise the
    // shell that owns the panel.
    connect(m_imageViewer, &ImageViewer::analysisRequested, this,
            [this](const QString &analyzerId)
            {
                if (!m_analysisPanel || !m_imageViewer)
                    return;
                const bool fullscreen =
                    m_imageViewer->property("mviewerFullscreenRequested").toBool() ||
                    m_imageViewer->isFullScreen() ||
                    m_imageViewer->windowState().testFlag(Qt::WindowFullScreen);
                if (fullscreen)
                    m_imageViewer->setFullscreenRequested(false);
                m_analysisPanel->setVisible(true);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(true);
                if (fullscreen)
                {
                    raise();
                    activateWindow();
                }
                m_analysisPanel->runAnalyzer(analyzerId);
            });
    wireHostedFileTransforms();
    connect(m_imageViewer, &ImageViewer::fileRotated, this,
            [this](const QString &path)
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->invalidateSourceImage(path);
                if (path == currentImagePath())
                {
                    if (m_previewPanel)
                        m_previewPanel->setImage(path);
                    if (m_metadataPanel)
                        m_metadataPanel->setImage(path);
                }
            });
    connect(m_imageViewer, &ImageViewer::pixelInfo, this,
            [this](int x, int y, int r, int g, int b, int a, int r16, int g16, int b16, int rawKind,
                   bool valid)
            {
                updateViewerPixelStatus(statusBar(), x, y, r, g, b, a, r16, g16, b16, rawKind,
                                        valid);
                m_analysisPanel->showPixel(x, y, r, g, b, a, r16, g16, b16, rawKind, valid);
            });
    if (m_lblZoom)
    {
        m_lblZoom->setCursor(Qt::PointingHandCursor);
        m_lblZoom->setToolTip(
            tr("Click to open the zoom preset menu, double-click to toggle Fit/100%"));
        m_lblZoom->installEventFilter(this);
    }
    if (m_lblImage)
    {
        m_lblImage->setCursor(Qt::PointingHandCursor);
        m_lblImage->setToolTip(tr("Click to locate the current file in File Explorer; right-click "
                                  "to copy the full path"));
        m_lblImage->installEventFilter(this);
    }
}

void MainWindow::wireHostedFileTransforms()
{
    if (!m_imageViewer)
        return;
    connect(m_imageViewer, &ImageViewer::hostRotateRequested, this,
            [this](const QString &path, int degrees)
            {
                if (!path.isEmpty())
                    runListedTransform({path}, true, degrees, false);
            });
    connect(m_imageViewer, &ImageViewer::hostFlipRequested, this,
            [this](const QString &path, bool horizontal)
            {
                if (!path.isEmpty())
                    runListedTransform({path}, false, 0, horizontal);
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

    connect(m_thumbnailPanel, &ThumbnailPanel::sortChanged, this,
            [this](ThumbnailPanel::SortMode mode, bool ascending)
            {
                if (m_sortCombo)
                {
                    const int idx = m_sortCombo->findData(mode);
                    if (idx >= 0 && m_sortCombo->currentIndex() != idx)
                    {
                        QSignalBlocker b(m_sortCombo);
                        m_sortCombo->setCurrentIndex(idx);
                    }
                }
                if (m_sortDirBtn)
                {
                    const bool descending = !ascending;
                    if (m_sortDirBtn->isChecked() != descending)
                    {
                        QSignalBlocker b(m_sortDirBtn);
                        m_sortDirBtn->setChecked(descending);
                        m_sortDirBtn->setText(descending ? "↓" : "↑");
                    }
                }
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
                // Same pipeline as keyboard and context-menu ratings: repaint
                // and re-evaluate an active rating/flag filter.
                m_thumbnailPanel->invalidateRatings();
            });
}

void MainWindow::connectMenuSignals()
{
    // ----- Menu actions -----
    connect(m_actOpenDir, &QAction::triggered, this,
            [this]()
            {
                const QString dir = QFileDialog::getExistingDirectory(this, "Open Directory");
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
                if (m_directoryMonitor)
                    m_directoryMonitor->reconcileNow();
            });
    connect(m_actOpenCurrentFolder, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->openCurrentFolder();
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
                    statusBar()->showMessage(tr("Path does not exist: %1").arg(text), 5000);
                    if (!currentDir().isEmpty())
                        m_pathEdit->setText(QDir::toNativeSeparators(currentDir()));
                }
            });
    connect(m_actOpenFile, &QAction::triggered, this,
            [this]()
            {
                QString filter = tr("Image files (");
                for (const auto &w : mviewer::core::ImageFormats::wildcardFilters())
                    filter += QString::fromStdString(w) + " ";
                filter = filter.trimmed() + ";;" + tr("All files (*)");
                const QString file = QFileDialog::getOpenFileName(
                    this, tr("Open Image"), currentDir().isEmpty() ? QString() : currentDir(),
                    filter);
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
                    QString("Unable to load image: %1").arg(QFileInfo(path).fileName()), 5000);
            });
    connect(m_imageViewer, &ImageViewer::statusMessageRequested, this,
            [this](const QString &msg, int timeoutMs)
            {
                if (isVisible() && statusBar())
                    statusBar()->showMessage(msg, timeoutMs);
            });
    connect(m_actSaveWorkspace, &QAction::triggered, this, &MainWindow::saveWorkspace);
    connect(m_actOpenWorkspace, &QAction::triggered, this, &MainWindow::openWorkspace);
    connect(m_actSaveProject, &QAction::triggered, this, &MainWindow::saveProject);
    connect(m_actOpenProject, &QAction::triggered, this, &MainWindow::openProject);
    connect(m_actExportReport, &QAction::triggered, this, &MainWindow::exportReport);
    connect(m_actExportImages, &QAction::triggered, this, &MainWindow::exportImages);
    connect(m_actExit, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_actCompare, &QAction::triggered, this,
            [this]() { m_thumbnailPanel ? m_thumbnailPanel->requestCompare() : openCompare(); });
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
    connect(m_searchPanel, &SearchPanel::reindexRequested, this, &MainWindow::reindexSearch);
    connect(m_actBatch, &QAction::triggered, this,
            [this]()
            {
                if (!m_batchDialog)
                    m_batchDialog = new BatchDialog(this);
                QStringList in = resolveSelectedPaths(true);
                m_batchDialog->setInputFiles(in.isEmpty() ? m_imageList->paths() : in);
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
                                statusBar()->showMessage(tr("Plugin list updated"), 3000);
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
                    this, tr("Export Settings"), QString(),
                    tr("MViewer Settings File (*.mvs);;All Files (*)"));
                if (path.isEmpty())
                    return;
                std::string err;
                if (mviewer::core::exportSettings(path.toStdString(), &err))
                    QMessageBox::information(this, tr("Export Settings"),
                                             tr("Settings exported to %1").arg(path));
                else
                    QMessageBox::warning(this, tr("Export Settings"),
                                         tr("Export failed: %1").arg(QString::fromStdString(err)));
            });
    connect(m_actImportSettings, &QAction::triggered, this,
            [this]()
            {
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("Import Settings"), QString(),
                    tr("MViewer Settings File (*.mvs);;All Files (*)"));
                if (path.isEmpty())
                    return;
                std::string err;
                if (mviewer::core::importSettings(path.toStdString(), &err))
                {
                    QMessageBox::information(this, tr("Import Settings"),
                                             tr("Settings imported. Some changes require "
                                                "restarting MViewer to take effect."));
                }
                else
                    QMessageBox::warning(this, tr("Import Settings"),
                                         tr("Import failed: %1").arg(QString::fromStdString(err)));
            });
    connect(m_actAbout, &QAction::triggered, this,
            [this]()
            {
                QMessageBox::about(this, tr("About MViewer"),
                                   tr("MViewer "
                                      "%1\n\nVisual analysis platform for imaging algorithm "
                                      "engineers: browse, compare, analyze, export.\n\nBuild: %2")
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
