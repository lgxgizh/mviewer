// MainWindow view behaviors: drag&drop, overlays, fullscreen, slideshow, status (M20 P0#1).
#include "mainwindow_p.h"

#include "application/ExternalOpen.h"

// M15: drag & drop — accept files/folders dropped onto the window.
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        m_dragHighlight = true;
        update();
    }
    else
        QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    // Accept moves anywhere on the window (including splitter handles and
    // status-bar edges) so the drop cursor never flickers to "forbidden".
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        if (!m_dragHighlight)
        {
            m_dragHighlight = true;
            update();
        }
    }
    else
        QMainWindow::dragMoveEvent(event);
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    QMainWindow::dragLeaveEvent(event);
    if (m_dragHighlight)
    {
        m_dragHighlight = false;
        update();
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    // Draw a translucent accent border while a drag-hover is active so the
    // user gets visual confirmation that a drop is accepted.
    if (m_dragHighlight)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = palette().color(QPalette::Highlight);
        QPen pen(accent, 4);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(2, 2, -2, -2));
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // Drop received — turn off the drag highlight regardless of outcome.
    if (m_dragHighlight)
    {
        m_dragHighlight = false;
        update();
    }
    if (!event->mimeData()->hasUrls())
    {
        QMainWindow::dropEvent(event);
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList paths;
    for (const QUrl &url : urls)
    {
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            paths.append(local);
    }
    if (paths.isEmpty())
    {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    handleDroppedPaths(paths);
}

void MainWindow::openExternalTargets(const QStringList &paths)
{
    const auto plan = mviewer::application::planExternalOpen(paths);
    if (!plan.isValid())
    {
        const QString message = plan.error.isEmpty() ? QStringLiteral("无法打开目标") : plan.error;
        statusBar()->showMessage(message);
        qWarning().noquote() << message;
        return;
    }

    switch (plan.kind)
    {
    case mviewer::application::ExternalOpenKind::Image:
        if (plan.isCompare())
            openCompare(plan.paths);
        else
            onImageOpen(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Directory:
        changeDirectory(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Workspace:
        openWorkspaceFile(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Project:
        openProjectFile(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Invalid:
        break;
    }
}

void MainWindow::handleDroppedPaths(const QStringList &paths)
{
    openExternalTargets(paths);
}

void MainWindow::showMetadataOverlay()
{
    if (!m_metadataOverlay || currentImagePath().isEmpty())
        return;
    m_metadataOverlay->showForImage(currentImagePath());
}

// P0-3: cancel the in-flight metadata-histogram task and invalidate the
// generation so any already-queued delivery is dropped by applyMetadataHistogram.
// This is the single logical invalidation: it bumps the generation once, clears
// the path/frame identity, and forgets any delivered state. Every schedule for a
// genuinely new request starts by canceling (which hands out the fresh gen).
void MainWindow::cancelMetadataHistogram()
{
    TaskScheduler::cancel(m_metadataHistTask);
    m_metadataHistTask.reset();
    ++m_metadataHistGen; // invalidate all prior deliveries
    m_metadataHistPath.clear();
    m_metadataHistFrame.reset();
    m_metadataHistDelivered = false;
}

// P0-3: snapshot the viewer's current frame (pixels by value — a cheap shared
// buffer) and the expected path, then compute the full-image histogram on the
// Analysis pool. The worker only touches captured values (pixels/path/gen; the
// QPointer is a mere delivery token, never dereferenced off the UI thread); the
// UI delivery is queued through qApp and re-validated on the main thread.
void MainWindow::scheduleMetadataHistogram()
{
    if (!m_metadataOverlay || !m_metadataOverlay->isVisible() || !m_imageViewer)
        return;
    auto frame = m_imageViewer->frame();
    if (!frame || frame->pixels().isNull())
        return; // no decoded frame yet; imageReady drives the schedule later
    const QString path = currentImagePath();
    if (path.isEmpty())
        return;
    // The frame must belong to the CURRENT image. While a new decode is in
    // flight the viewer still holds the previous frame; scheduling it now would
    // compute the wrong pixels (and the imageReady delivery re-schedules).
    if (QString::fromStdString(frame->metadata().filePath) != path)
        return;
    // Already delivered for this exact path+frame: repeated visibility
    // notifications (hover, action, imageReady) must not resubmit.
    if (m_metadataHistDelivered && m_metadataHistPath == path &&
        m_metadataHistFrame.lock() == frame)
        return;
    // Latest-wins dedup: an in-flight request for the same path+frame is already
    // the newest work — do not stack a second Analysis task for one show.
    if (m_metadataHistTask && m_metadataHistPath == path &&
        m_metadataHistFrame.lock() == frame)
        return;

    // A new request supersedes whatever was in flight or already delivered;
    // cancel hands out the fresh generation (one bump, no double increments).
    cancelMetadataHistogram();
    const uint64_t gen = m_metadataHistGen;
    m_metadataHistPath = path;
    m_metadataHistFrame = frame; // identity token (weak; viewer owns the frame)
    const ImageData pixels = frame->pixels();
    const QString expectedPath = path;

    QPointer<MainWindow> guard(this);
    auto task = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, expectedPath, gen, guard](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return;
            const mviewer::core::Histogram hist = mviewer::core::computeHistogram(pixels);
            if (ctx.isCancelled())
                return; // superseded mid-computation — never deliver stale work
            QMetaObject::invokeMethod(
                qApp,
                [gen, expectedPath, hist, guard]()
                {
                    MainWindow *win = guard.data();
                    if (!win)
                        return; // MainWindow destroyed while computing
                    win->applyMetadataHistogram(gen, expectedPath, hist);
                },
                Qt::QueuedConnection);
        });
    if (!task)
        return; // back-pressure/rejected: never fall back to synchronous compute
    m_metadataHistTask = task;
}

// P0-3: apply a computed histogram only when it is still the newest request for
// the overlay's current image. Runs on the UI thread via a queued qApp lambda.
void MainWindow::applyMetadataHistogram(uint64_t gen, const QString &path,
                                        const mviewer::core::Histogram &hist)
{
    if (gen != m_metadataHistGen || path != currentImagePath())
        return; // superseded by a newer request or a different image
    if (!m_metadataOverlay || !m_metadataOverlay->isVisible())
        return; // overlay dismissed while computing
    if (m_metadataHistFrame.lock() != (m_imageViewer ? m_imageViewer->frame() : nullptr))
        return; // the viewer replaced the frame (e.g. a reload of the same path)
    m_metadataOverlay->setHistogram(hist);
    // Delivery succeeded: release the completed scheduler handle so the task
    // graph stays clean, and remember that this exact path+frame is delivered so
    // repeated show notifications short-circuit in scheduleMetadataHistogram.
    m_metadataHistTask.reset();
    m_metadataHistDelivered = true;
}

// A-10: refresh Undo/Redo menu labels and enabled state from CommandStack.
void MainWindow::updateUndoRedoActions()
{
    if (m_actUndo)
    {
        m_actUndo->setEnabled(m_cmdStack.canUndo());
        const std::string label = m_cmdStack.undoLabel();
        m_actUndo->setText(label.empty()
                               ? QStringLiteral("撤销(&U)")
                               : QStringLiteral("撤销(&U) %1").arg(QString::fromStdString(label)));
    }
    if (m_actRedo)
    {
        m_actRedo->setEnabled(m_cmdStack.canRedo());
        const std::string label = m_cmdStack.redoLabel();
        m_actRedo->setText(label.empty()
                               ? QStringLiteral("重做(&R)")
                               : QStringLiteral("重做(&R) %1").arg(QString::fromStdString(label)));
    }
}

// A-5: position the floating MetadataPanel on the right edge of the main window.
void MainWindow::positionMetadataPanel()
{
    if (!m_metadataPanel)
        return;
    const QPoint topRight = mapToGlobal(QPoint(width(), 0));
    const int x = topRight.x() - m_metadataPanel->width() - 16;
    const int y = topRight.y() + 80;
    m_metadataPanel->move(x, y);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    if (m_metadataPanel && m_metadataPanel->isVisible())
        positionMetadataPanel();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_metadataPanel && m_metadataPanel->isVisible())
        positionMetadataPanel();
}

void MainWindow::toggleMetadataOverlay()
{
    if (currentImagePath().isEmpty())
        return;
    // A-5: toggle both the viewer overlay and the floating MetadataPanel.
    const bool show = !(m_metadataOverlay && m_metadataOverlay->isVisible()) &&
                      !(m_metadataPanel && m_metadataPanel->isVisible());
    if (show)
    {
        if (m_metadataOverlay)
            m_metadataOverlay->showForImage(currentImagePath());
        if (m_metadataPanel)
        {
            m_metadataPanel->show();
            m_metadataPanel->raise();
            positionMetadataPanel();
            // Showing before setImage lets the panel request metadata through
            // the shared service. Hidden panel updates remain zero-work.
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
    // P0-3: keep the "图片信息" toggle in the View menu in sync so every entry
    // point (Ctrl+I, M key, ESC) agrees on the overlay's state.
    if (m_actToggleMetadata)
        m_actToggleMetadata->setChecked(show);
}

void MainWindow::copyCurrentImageToClipboard()
{
    if (currentImagePath().isEmpty())
        return;
    // Keep MainWindow and the viewer on one Copy Image pipeline. The worker
    // performs repository decode + ICC/display conversion; only the final
    // QClipboard assignment returns to the GUI thread.
    if (m_imageViewer)
        m_imageViewer->copyToClipboard(currentImagePath());
}

void MainWindow::toggleFullscreen()
{
    if (m_imageViewer && m_imageViewer->isVisible())
        m_imageViewer->toggleFullscreen();
    else if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::toggleFocusBrowse()
{
    if (!m_navigationWidget || !m_analysisPanel || !m_searchPanel)
        return;

    if (!m_focusBrowse)
    {
        m_focusNavigationVisible = m_navigationWidget->isVisible();
        m_focusAnalysisVisible = m_analysisPanel->isVisible();
        m_focusSearchVisible = m_searchPanel->isVisible();
        m_focusBrowse = true;
        m_navigationWidget->hide();
        m_analysisPanel->hide();
        m_searchPanel->hide();
        m_actToggleAnalysis->setEnabled(false);
        m_actToggleSearch->setEnabled(false);
    }
    else
    {
        m_focusBrowse = false;
        m_navigationWidget->setVisible(m_focusNavigationVisible);
        m_analysisPanel->setVisible(m_focusAnalysisVisible);
        m_searchPanel->setVisible(m_focusSearchVisible);
        m_actToggleAnalysis->setEnabled(true);
        m_actToggleSearch->setEnabled(true);
        m_actToggleAnalysis->setChecked(m_focusAnalysisVisible);
        m_actToggleSearch->setChecked(m_focusSearchVisible);
    }
    if (m_actFocusBrowse)
        m_actFocusBrowse->setChecked(m_focusBrowse);
}

void MainWindow::openPreferences()
{
    PreferencesDialog dlg(this);
    connect(&dlg, &PreferencesDialog::settingsChanged, this, &MainWindow::applyPreferences);
    dlg.exec();
}

void MainWindow::applyPreferences()
{
    QSettings s;
    const int vm = s.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt();
    if (m_thumbnailPanel)
        m_thumbnailPanel->setViewMode(static_cast<ThumbnailPanel::ViewMode>(vm));
    const int sm = s.value("thumbSortMode", ThumbnailPanel::SortName).toInt();
    if (m_sortCombo)
    {
        for (int i = 0; i < m_sortCombo->count(); ++i)
        {
            if (m_sortCombo->itemData(i).toInt() == sm)
            {
                m_sortCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_thumbnailPanel)
        m_thumbnailPanel->setThumbSize(s.value("thumbSize", 160).toInt());
    if (m_slideshowTimer)
    {
        const int interval = s.value("slideshowInterval", 3000).toInt();
        m_slideshowTimer->setInterval(interval);
        if (m_slideshowTimer->isActive())
            m_slideshowTimer->start(interval);
    }
    if (m_imageViewer)
        m_imageViewer->setZebraThreshold(s.value("zebraThreshold", 2).toInt());
}

void MainWindow::openAnalysisOverlay()
{
    const QString path = currentImagePath();
    if (path.isEmpty())
        return;
    QImage img(path);
    if (img.isNull())
        return;
    AnalysisOverlayDialog dlg(img, this);
    dlg.exec();
}

void MainWindow::toggleSlideshow()
{
    if (m_slideshowTimer && m_slideshowTimer->isActive())
    {
        stopSlideshow();
        return;
    }
    if (currentImagePath().isEmpty() || currentDir().isEmpty())
    {
        statusBar()->showMessage("请先选择一张图片再开始幻灯片放映", 3000);
        if (m_actSlideshow)
            m_actSlideshow->setChecked(false);
        return;
    }
    // Fullscreen the viewer for the slideshow; ESC (or S) stops it.
    onImageOpen(currentImagePath());
    if (!m_imageViewer->property("mviewerFullscreenRequested").toBool())
        m_imageViewer->setFullscreenRequested(true);
    // Read interval from settings (default 3s), allow user to change via
    // a simple input dialog triggered by Ctrl+Shift+S.
    QSettings settings;
    int interval = settings.value("slideshowInterval", 3000).toInt();
    interval = qBound(500, interval, 60000); // clamp 0.5s–60s
    if (!m_slideshowTimer)
    {
        m_slideshowTimer = new QTimer(this);
        connect(m_slideshowTimer, &QTimer::timeout, this,
                [this]()
                {
                    // Closing the viewer (ESC) ends the show.
                    if (m_imageViewer->isHidden())
                    {
                        stopSlideshow();
                        return;
                    }
                    navigate(1); // wraps at the end of the folder
                });
    }
    m_slideshowTimer->start(interval);
    if (m_actSlideshow)
        m_actSlideshow->setChecked(true);
    statusBar()->showMessage(
        QString("幻灯片放映中 — 按 S 或 ESC 停止 (间隔 %1 秒)").arg(interval / 1000.0, 0, 'f', 1),
        3000);
}

void MainWindow::stopSlideshow()
{
    if (m_slideshowTimer)
        m_slideshowTimer->stop();
    if (m_actSlideshow)
        m_actSlideshow->setChecked(false);
    statusBar()->showMessage("幻灯片放映已停止", 2000);
}

void MainWindow::zoomViewer(int op)
{
    // Zoom commands only make sense while the viewer is on screen.
    if (m_imageViewer->isHidden())
        return;
    switch (op)
    {
    case 0:
        m_imageViewer->zoomIn();
        break;
    case 1:
        m_imageViewer->zoomOut();
        break;
    case 2:
        m_imageViewer->zoomFit();
        break;
    case 3:
        m_imageViewer->zoomActual();
        break;
    }
}

void MainWindow::openQuickCompare()
{
    if (currentImagePath().isEmpty())
        return;
    ensureImageList();
    QStringList imgs;
    imgs << currentImagePath();
    const int idx = m_imageList->indexOf(currentImagePath());
    if (idx >= 0 && idx + 1 < m_imageList->count())
        imgs << m_imageList->pathAt(idx + 1);
    else if (idx != 0 && !m_imageList->isEmpty())
        imgs << m_imageList->pathAt(0);
    openCompare(imgs);
}

// P0 #①: status bar helpers.

QString MainWindow::formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QString::number(kb, 'f', 1) + " KB";
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QString::number(mb, 'f', 1) + " MB";
    const double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}

void MainWindow::updateCacheStat()
{
    if (!m_lblCache)
        return;
    auto &cm = CacheManager::instance();
    uint64_t hits = 0, misses = 0;
    for (CacheLevel lvl :
         {CacheLevel::Metadata, CacheLevel::Thumbnail, CacheLevel::Preview, CacheLevel::FullImage})
    {
        const CacheLevelStats s = cm.levelStats(lvl);
        hits += s.hits;
        misses += s.misses;
    }
    if (hits + misses == 0)
        m_lblCache->setText("命中率 —");
    else
        m_lblCache->setText(QString("命中率 %1%").arg(int(100.0 * hits / (hits + misses))));
}

// P0-3: click / hover on the image viewer shows the metadata overlay.
// P1-4: also forward global workflow shortcuts from child widgets.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        // While the viewer window has focus (e.g. slideshow fullscreen), 'S'
        // still toggles the slideshow; the viewer itself has no such binding.
        if (watched == m_imageViewer && ke->key() == Qt::Key_S && !ke->modifiers())
        {
            toggleSlideshow();
            return true;
        }
        // Forward navigation / workflow shortcuts from child widgets so they work
        // regardless of which panel has focus.
        static const QList<int> globalKeys = {
            Qt::Key_Space, Qt::Key_M, Qt::Key_H,    Qt::Key_G,    Qt::Key_D,      Qt::Key_F,
            Qt::Key_Tab,   Qt::Key_C, Qt::Key_S,    Qt::Key_Plus, Qt::Key_Equal,  Qt::Key_Minus,
            Qt::Key_0,     Qt::Key_1, Qt::Key_Home, Qt::Key_End,  Qt::Key_PageUp, Qt::Key_PageDown};
        const bool isGlobalKey =
            globalKeys.contains(ke->key()) ||
            ((ke->modifiers() & Qt::ControlModifier) &&
             (ke->key() == Qt::Key_C || (ke->key() >= Qt::Key_1 && ke->key() <= Qt::Key_6)));
        if (isGlobalKey && watched != this)
        {
            // Also forward from the image viewer (it has its own keyPressEvent
            // that handles zoom/navigation, but Home/End/PageUp/PageDown and
            // workflow keys like C/S/Space should still reach MainWindow).
            if (watched == m_imageViewer)
            {
                // Only forward keys the viewer doesn't handle itself.
                static const QSet<int> viewerOwns = {
                    Qt::Key_Left,  Qt::Key_Right,  Qt::Key_Plus,      Qt::Key_Equal,
                    Qt::Key_Minus, Qt::Key_0,      Qt::Key_1,         Qt::Key_F,
                    Qt::Key_F11,   Qt::Key_Escape, Qt::Key_Underscore};
                if (viewerOwns.contains(ke->key()))
                    return false; // let the viewer handle it
            }
            keyPressEvent(ke);
            return true;
        }
    }

    if (watched == m_imageViewer)
    {
        if (event->type() == QEvent::MouseButtonPress && !currentImagePath().isEmpty())
        {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton)
            {
                if (m_metadataOverlay && m_metadataOverlay->isVisible())
                    m_metadataOverlay->hide();
                else
                    showMetadataOverlay();
            }
        }
        else if (event->type() == QEvent::HoverMove || event->type() == QEvent::MouseMove)
        {
            if (m_metadataHoverTimer && !currentImagePath().isEmpty())
            {
                m_metadataHoverTimer->stop();
                m_metadataHoverTimer->start();
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            if (m_metadataHoverTimer)
                m_metadataHoverTimer->stop();
        }
    }
    // Keep the gallery overlay hints sized to the panel as the window
    // (and therefore the gallery) resizes.
    if (watched == m_thumbnailPanel && event->type() == QEvent::Resize)
    {
        if (m_emptyState)
            m_emptyState->setGeometry(m_thumbnailPanel->rect());
        if (m_emptyFolderLabel && m_emptyFolderLabel->isVisible())
            m_emptyFolderLabel->setGeometry(m_thumbnailPanel->rect());
    }
    return QMainWindow::eventFilter(watched, event);
}
