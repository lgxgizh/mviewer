// MainWindow session restore, recovery and close persistence.
#include "mainwindow_p.h"

#include "runtime_storage.h"

#include <QSaveFile>

namespace
{

QString appConfigFile(const QString &name)
{
    return mviewer::runtime::filePath(QStandardPaths::AppConfigLocation, name);
}

} // namespace

void MainWindow::restoreLastSession()
{
    // Defer to the next event loop tick so the thumbnail worker has started and
    // setDirectory() has populated items before we try to scroll/select.
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            // P1-3: restore window layout (splitter + view mode) before populating widgets.
            QSettings settings;
            if (m_mainSplitter)
                m_mainSplitter->restoreState(settings.value("splitterState").toByteArray());
            const int vm = settings.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt();
            if (m_thumbnailPanel)
                m_thumbnailPanel->setViewMode(static_cast<ThumbnailPanel::ViewMode>(vm));
            const int ts = settings.value("thumbSize", ThumbnailPanel::kDefaultThumbSize).toInt();
            if (m_thumbnailPanel)
                m_thumbnailPanel->setThumbSize(ts);
            if (m_thumbSizeSlider)
                m_thumbSizeSlider->setValue(m_thumbnailPanel ? m_thumbnailPanel->thumbSize() : ts);

            // P1-3: restore the Analysis workspace so the UI reopens where left off.
            if (m_analysisPanel)
            {
                m_analysisPanel->setVisible(m_appState.analysisVisible);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(m_appState.analysisVisible);
                m_analysisPanel->setCurrentPage(m_appState.analysisPage);
            }
            // Restore search panel visibility.
            const bool searchVisible = settings.value("searchVisible", false).toBool();
            if (m_searchPanel)
                m_searchPanel->setVisible(searchVisible);
            if (m_actToggleSearch)
                m_actToggleSearch->setChecked(searchVisible);

            const QString dir = m_appState.lastDir;
            if (dir.isEmpty() || !QDir(dir).exists())
                return;
            changeDirectory(dir);

            const QString img = m_appState.lastImage;
            if (!img.isEmpty() && QFile::exists(img))
            {
                pushHistory(img);
                m_selection->setCurrentImage(img);
                m_imageViewer->setImage(img);  // async; imageReady() feeds AnalysisPanel
                m_previewPanel->setImage(img); // async; off UI thread
                m_metadataPanel->setImage(img);
                if (m_metadataOverlay)
                    m_metadataOverlay->setImage(img);
            }

            // P1-3: restore the full navigation history stack (browser back/forward
            // + History sidebar) so reopening lands the user mid-browse, not just
            // on the last image. Drop entries whose files no longer exist.
            QStringList restoredHist;
            for (const QString &p : m_appState.navHistory)
                if (QFile::exists(p))
                    restoredHist.append(p);
            if (!restoredHist.isEmpty())
            {
                m_history = restoredHist;
                int idx = m_appState.navHistoryIndex;
                if (idx < 0 || idx >= m_history.size())
                    idx = m_history.size() - 1;
                m_historyIndex = idx;
                // Feed the History sidebar panel from the restored stack.
                m_appState.history = m_history;
                updateSelectionActions();
            }
            // Restore the thumbnail-grid scroll position after items exist.
            QMetaObject::invokeMethod(
                this,
                [this]()
                {
                    if (m_appState.lastThumbScroll > 0)
                        m_thumbnailPanel->verticalScrollBar()->setValue(m_appState.lastThumbScroll);
                    if (!m_appState.lastImage.isEmpty())
                        m_thumbnailPanel->scrollToPath(m_appState.lastImage);

                    // A-6.3: restore viewer zoom/pan from QSettings (same logic as
                    // crash-recovery path, but for normal session restore).
                    QSettings vs;
                    if (m_imageViewer && !currentImagePath().isEmpty() &&
                        vs.value("viewerPath").toString() == currentImagePath())
                    {
                        Viewport v;
                        v.screenW = m_imageViewer->width();
                        v.screenH = m_imageViewer->height();
                        v.scale = vs.value("viewerScale", 1.0).toReal();
                        v.offsetX = vs.value("viewerOffX", 0.0).toReal();
                        v.offsetY = vs.value("viewerOffY", 0.0).toReal();
                        m_imageViewer->setViewTransform(v);
                    }

                    // A-6.1: restore Compare session on normal startup (not just
                    // crash recovery). If QSettings has a compareSession, reopen it.
                    const QJsonArray cmpImgs = vs.value("compareImages").toJsonArray();
                    const QString cmpSession = vs.value("compareSession").toString();
                    QStringList cmpPaths;
                    for (const auto &v2 : cmpImgs)
                    {
                        const QString p = v2.toString();
                        if (!p.isEmpty() && QFile::exists(p))
                            cmpPaths.append(p);
                    }
                    if (cmpPaths.size() >= 2 && !cmpSession.isEmpty())
                        openCompare(cmpPaths, cmpSession);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Persist browse position for next launch (P0 cross-session restore).
    m_appState.lastDir = currentDir();
    m_appState.lastImage = currentImagePath();
    m_appState.lastThumbScroll = m_thumbnailPanel ? m_thumbnailPanel->scrollOffset() : 0;

    // P1-3: persist the Analysis workspace so reopening restores UI state.
    // Focus Browse temporarily hides panels; persist the state from before that
    // temporary mode so closing in Focus does not silently change preferences.
    // M25: same rule for the Browse workspace — closing while Browse is active
    // must restore the analysis/search state the user had BEFORE entering
    // Browse, not the hidden Browse layout.
    const bool inBrowseWorkspace = m_actBrowseWorkspace && m_actBrowseWorkspace->isChecked();
    m_appState.analysisVisible =
        inBrowseWorkspace ? m_browseAnalysisVisible
                          : (m_focusBrowse ? m_focusAnalysisVisible
                                           : (m_analysisPanel && m_analysisPanel->isVisible()));
    m_appState.analysisPage = m_analysisPanel ? m_analysisPanel->currentPage() : 0;

    // P1-3: persist the navigation history stack (browser back/forward + History
    // panel) so reopening restores exactly where the user was browsing.
    m_appState.navHistory = m_history;
    m_appState.navHistoryIndex = m_historyIndex;
    m_appState.save();

    // M16: persist analysis history / pinned results so they survive restart.
    if (m_analyzer)
        m_analyzer->save();

    // Normal exit: remove the crash-recovery marker so the next launch doesn't
    // prompt for a restore (only an unclean shutdown leaves it behind).
    {
        const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
        QFile::remove(recoveryPath);
    }

    // Persist the recent-folders LRU alongside app state.
    const QString recentPath = appConfigFile(QStringLiteral("recent.json"));
    QSaveFile rf(recentPath);
    if (!recentPath.isEmpty() && rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (rf.write(QByteArray::fromStdString(m_recent.serialize())) >= 0)
            (void)rf.commit();
    }

    // M13.5 / P1-3: persist window geometry/layout (QSettings, independent of workspace).
    {
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        // P1-3: persist thumbnail view mode and splitter geometry.
        if (m_thumbnailPanel)
            settings.setValue("thumbViewMode", m_thumbnailPanel->viewMode());
        if (m_thumbnailPanel)
            settings.setValue("thumbSize", m_thumbnailPanel->thumbSize());
        if (m_sortCombo)
            settings.setValue("thumbSortMode", m_sortCombo->currentData().toInt());
        if (m_mainSplitter)
            settings.setValue("splitterState", m_mainSplitter->saveState());
        if (m_searchPanel && m_actToggleSearch)
        {
            const bool searchVisible =
                inBrowseWorkspace
                    ? m_browseSearchVisible
                    : (m_focusBrowse ? m_focusSearchVisible : m_searchPanel->isVisible());
            settings.setValue("searchVisible", searchVisible);
        }
        // P1-7: persist the main viewer's zoom level + pan position so a session
        // that ended with the viewer open restores identically (scale/offset are
        // screen-space, so the viewer must have been visible to be meaningful).
        if (m_imageViewer && !m_imageViewer->isHidden() && !currentImagePath().isEmpty())
        {
            const auto v = m_imageViewer->viewTransform();
            settings.setValue("viewerPath", currentImagePath());
            settings.setValue("viewerScale", v.scale);
            settings.setValue("viewerOffX", v.offsetX);
            settings.setValue("viewerOffY", v.offsetY);
        }
        // A-6.1: persist Compare session for normal startup restore (not just
        // crash recovery). Same format as autosaveSession().
        if (m_compareView && m_compareView->comparedImageCount() >= 2)
        {
            const auto cs = m_compareView->compareSession();
            QJsonArray cmpImg;
            for (const auto &id : cs.imageIds)
                cmpImg.append(QString::fromUtf8(id.data(), static_cast<int>(id.size())));
            settings.setValue("compareImages", cmpImg);
            settings.setValue("compareSession",
                              QString::fromStdString(mviewer::core::serializeCompareSession(cs)));
        }
        else
        {
            settings.remove("compareImages");
            settings.remove("compareSession");
        }
        // A-6.4: persist left-column width (main splitter index 0) as a plain
        // int so it can be restored even when analysis/search visibility changes.
        if (m_mainSplitter)
        {
            const QList<int> sizes = m_mainSplitter->sizes();
            if (!sizes.isEmpty())
                settings.setValue("navSidebarWidth", sizes[0]);
        }
        // A-6.4: persist vertical proportions of the left sidebar independently.
        if (m_leftSplitter)
            settings.setValue("leftSplitterState", m_leftSplitter->saveState());
        settings.sync();
    }

    QMainWindow::closeEvent(event);
}

// M15: crash recovery — autosave current session to a recovery file.
void MainWindow::autosaveSession()
{
    if (currentDir().isEmpty() && currentImagePath().isEmpty())
        return;
    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    if (recoveryPath.isEmpty())
        return;
    QSaveFile f(recoveryPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    // Simple JSON: lastDir, lastImage, lastThumbScroll, compare (M15 P0#1)
    QJsonObject obj;
    obj.insert("lastDir", currentDir());
    obj.insert("lastImage", currentImagePath());
    obj.insert("lastThumbScroll", m_thumbnailPanel ? m_thumbnailPanel->scrollOffset() : 0);

    // M15 P0#1: also persist the live Compare session (images + full state) so a
    // crash can restore Compare, not just the gallery/single view.
    if (m_compareView && m_compareView->comparedImageCount() >= 2)
    {
        const auto cs = m_compareView->compareSession();
        QJsonArray cmpImg;
        for (const auto &id : cs.imageIds)
            cmpImg.append(QString::fromUtf8(id.data(), static_cast<int>(id.size())));
        obj.insert("compareImages", cmpImg);
        obj.insert("compareSession",
                   QString::fromStdString(mviewer::core::serializeCompareSession(cs)));
    }

    obj.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QJsonDocument doc(obj);
    if (f.write(doc.toJson()) >= 0)
        (void)f.commit();
}

// M15: crash recovery — restore session from recovery file if it exists.
void MainWindow::restoreSessionRecovery()
{
    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    if (recoveryPath.isEmpty())
        return;
    QFile f(recoveryPath);
    QByteArray data;
    if (!readBoundedStateFile(f, recoveryPath, kMaxRecoveryStateBytes, data))
        return;
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QString lastDir = obj.value("lastDir").toString();
    const QString lastImage = obj.value("lastImage").toString();
    const int lastThumbScroll = obj.value("lastThumbScroll").toInt();
    const QJsonArray compareImages = obj.value("compareImages").toArray();
    const QString compareSession = obj.value("compareSession").toString();

    if (lastDir.isEmpty() && lastImage.isEmpty() && compareImages.isEmpty())
        return;

    // Ask the user whether to restore the previous session. The recovery file
    // is a crash-recovery artifact; a normal exit clears it (see closeEvent),
    // so its presence implies an unclean shutdown.
    const auto answer = QMessageBox::question(
        this, tr("恢复上次会话"), tr("检测到上次会话未正常关闭。\n是否恢复上次浏览的图片和目录？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
    {
        QFile::remove(recoveryPath);
        return;
    }

    // M15 P0#1: restore the Compare session too. Only trust it if the recorded
    // images still exist on disk.
    QStringList cmpImgs;
    for (const auto &v : compareImages)
    {
        const QString p = v.toString();
        if (!p.isEmpty() && QFile::exists(p))
            cmpImgs.append(p);
    }
    const bool restoreCompare = cmpImgs.size() >= 2 && !compareSession.isEmpty();

    // Restore the session (deferred to event loop).
    QTimer::singleShot(
        100, this,
        [this, lastDir, lastImage, lastThumbScroll, cmpImgs, compareSession, restoreCompare]()
        {
            if (!lastDir.isEmpty() && QDir(lastDir).exists())
            {
                changeDirectory(lastDir);
                if (lastThumbScroll > 0)
                    m_thumbnailPanel->verticalScrollBar()->setValue(lastThumbScroll);
            }
            if (!lastImage.isEmpty() && QFile::exists(lastImage))
            {
                m_selection->setCurrentImage(lastImage);
                onImageOpen(lastImage);
                // P1-7: if the session ended with the viewer open on
                // this exact image, restore its zoom level + pan. The
                // transform is applied on the UI thread after the async
                // decode completes (see ImageViewer::setImage).
                QSettings vs;
                if (vs.value("viewerPath").toString() == lastImage)
                {
                    Viewport v;
                    v.screenW = m_imageViewer->width();
                    v.screenH = m_imageViewer->height();
                    v.scale = vs.value("viewerScale", 1.0).toReal();
                    v.offsetX = vs.value("viewerOffX", 0.0).toReal();
                    v.offsetY = vs.value("viewerOffY", 0.0).toReal();
                    m_imageViewer->setViewTransform(v);
                }
            }
            // M15 P0#1: reopen Compare with its fully persisted
            // session (ROI, zoom, layout, threshold, blink, ...).
            if (restoreCompare)
                openCompare(cmpImgs, compareSession);
            m_autosaveLoaded = true;
        });
}
