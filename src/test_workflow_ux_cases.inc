void workflow1_browse(const QString &dirPath, const QStringList &paths)
{
    // Reproduce the real upgrade path: the former sidebar persisted a splitter
    // with six independent children. Qt may partially apply that byte array to
    // the new three-section splitter unless the one-time migration replaces it.
    QSettings settings;
    settings.remove("browserSidebarLayoutVersion");
    {
        QSplitter legacySidebar(Qt::Vertical);
        for (int i = 0; i < 6; ++i)
            legacySidebar.addWidget(new QWidget(&legacySidebar));
        legacySidebar.resize(300, 700);
        legacySidebar.setSizes({20, 320, 160, 20, 20, 500});
        settings.setValue("leftSplitterState", legacySidebar.saveState());
        settings.sync();
    }

    std::cout << "── Workflow 1: browse ──\n";

    // A second folder makes the directory-switch contract observable: the
    // old selection must clear before the asynchronous first row arrives.
    QDir sourceDir(dirPath);
    const QString nextDir = sourceDir.filePath(QStringLiteral("wf_next"));
    sourceDir.mkpath(QStringLiteral("wf_next"));
    const QString nextPath =
        writePng(QDir(nextDir), QStringLiteral("next.png"), QColor(80, 120, 220));

    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    QDir().mkpath(QFileInfo(recoveryPath).absolutePath());
    QFile recoveryFile(recoveryPath);
    CHECK(recoveryFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "recovery fixture is written to the test AppConfig");
    if (recoveryFile.isOpen())
    {
        QJsonObject recovery;
        recovery.insert("lastDir", dirPath);
        recovery.insert("lastImage", paths.first());
        recoveryFile.write(QJsonDocument(recovery).toJson());
        recoveryFile.close();
    }

    MainWindow *window = nullptr;
    bool recoveryPromptSeen = false;
    bool recoveryParentVisible = false;
    bool recoveryParentMatches = false;
    QTimer recoveryPromptPoller;
    recoveryPromptPoller.setInterval(10);
    QObject::connect(&recoveryPromptPoller, &QTimer::timeout,
                     [&]()
                     {
                         for (QWidget *top : QApplication::topLevelWidgets())
                         {
                             auto *box = qobject_cast<QMessageBox *>(top);
                             if (!box || !box->isVisible())
                                 continue;
                             recoveryPromptSeen = true;
                             recoveryParentVisible = window && window->isVisible();
                             recoveryParentMatches = window && box->parentWidget() == window;
                             box->done(QMessageBox::No);
                             recoveryPromptPoller.stop();
                             return;
                         }
                     });
    recoveryPromptPoller.start();

    QElapsedTimer constructionTimer;
    constructionTimer.start();
    MainWindow w;
    window = &w;
    const qint64 constructionMs = constructionTimer.elapsed();
    CHECK(constructionMs < 5000, "recovery prompt does not block MainWindow construction");
    CHECK(QApplication::activeModalWidget() == nullptr,
          "recovery prompt is not modal during MainWindow construction");
    w.resize(1280, 800);
    w.show();
    pump(100);
    CHECK(recoveryPromptSeen, "recovery prompt appears after the main window is shown");
    CHECK(recoveryParentMatches && recoveryParentVisible,
          "recovery prompt is parented to the visible MainWindow");
    w.activateWindow();
    w.raise();
    pump(10);

    auto *analysisPanel = w.findChild<QWidget *>("analysisPanel");
    auto *searchPanel = w.findChild<QWidget *>("searchPanel");
    auto *navigationPanel = w.findChild<QWidget *>("navigationPanel");
    auto *advancedFilterPanel = w.findChild<QWidget *>("advancedFilterPanel");
    auto *advancedFilterToggle = w.findChild<QPushButton *>("advancedFilterToggle");
    auto *analysisAction = w.findChild<QAction *>("toggleAnalysisPanelAction");
    auto *searchAction = w.findChild<QAction *>("toggleSearchPanelAction");
    auto *focusAction = w.findChild<QAction *>("focusBrowseAction");
    auto *metadataAction = w.findChild<QAction *>("toggleMetadataAction");
    auto *zoomInAction = w.findChild<QAction *>("zoomInAction");
    auto *zoomOutAction = w.findChild<QAction *>("zoomOutAction");
    auto *zoomFitAction = w.findChild<QAction *>("zoomFitAction");
    auto *zoomActualAction = w.findChild<QAction *>("zoomActualAction");
    auto *slideshowAction = w.findChild<QAction *>("slideshowAction");
    auto *historyBackAction = w.findChild<QAction *>("historyBackAction");
    auto *historyForwardAction = w.findChild<QAction *>("historyForwardAction");
    auto *dirBackAction = w.findChild<QAction *>("directoryBackAction");
    auto *dirForwardAction = w.findChild<QAction *>("directoryForwardAction");
    auto *dirUpAction = w.findChild<QAction *>("directoryUpAction");
    auto *refreshAction = w.findChild<QAction *>("refreshDirectoryAction");
    auto *addFavoriteAction = w.findChild<QAction *>("addFavoriteAction");
    auto *removeFavoriteAction = w.findChild<QAction *>("removeFavoriteAction");
    QAction *compareAction = nullptr;
    for (QAction *action : w.findChildren<QAction *>())
        if (action && action->text().startsWith(QStringLiteral("比较模式")))
            compareAction = action;
    auto *browserToolBar = w.findChild<QToolBar *>("browserToolBar");
    auto *browseWorkspaceAction = w.findChild<QAction *>("browseWorkspaceAction");
    auto *foldersSection = w.findChild<QWidget *>("foldersSection");
    auto *previewSection = w.findChild<QWidget *>("previewSection");
    auto *directoryTree = w.findChild<DirectoryTree *>();
    auto *previewPanel = w.findChild<PreviewPanel *>();
    auto *gallerySearchEdit = w.findChild<QLineEdit *>("searchEdit");
    auto *viewModeCombo = w.findChild<QComboBox *>("thumbnailViewModeCombo");
    auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
    CHECK(browserToolBar != nullptr, "professional browser toolbar has a stable object name");
    CHECK(browserToolBar && browserToolBar->actions().size() >= 8,
          "browser toolbar exposes the high-frequency browse commands");
    CHECK(browseWorkspaceAction && browseWorkspaceAction->isCheckable(),
          "browse layout action is a stable checkable command");
    CHECK(foldersSection && previewSection,
          "navigation sidebar exposes stable folders and preview sections");
    QSettings migratedSettings;
    migratedSettings.sync();
    CHECK(migratedSettings.value("browserSidebarLayoutVersion").toInt() == 1,
          "legacy navigation sidebar layout is migrated exactly once");
    CHECK(foldersSection && previewSection && foldersSection->height() > previewSection->height(),
          "migrated sidebar gives the folder section the larger 60/40 share");
    CHECK(directoryTree && directoryTree->filterEdit() &&
              directoryTree->filterEdit()->height() <= 40,
          "directory filter keeps a normal editor height after layout restore");
    CHECK(directoryTree && directoryTree->isVisible() && directoryTree->height() >= 40,
          "directory tree remains visible with usable height");
    CHECK(previewPanel && previewPanel->isVisible() && previewPanel->height() >= 80,
          "preview panel remains visible with usable height");
    CHECK(analysisPanel && !analysisPanel->isVisible(),
          "clean startup keeps the analysis panel hidden");
    CHECK(searchPanel && !searchPanel->isVisible(), "clean startup keeps the search panel hidden");
    CHECK(advancedFilterPanel && !advancedFilterPanel->isVisible(),
          "advanced gallery filters are hidden by default");
    CHECK(advancedFilterToggle && advancedFilterToggle->isCheckable(),
          "advanced filter toggle has a stable interactive control");
    CHECK(gallerySearchEdit && gallerySearchEdit->isVisible() && gallerySearchEdit->isEnabled() &&
              gallerySearchEdit->width() > 0 && gallerySearchEdit->height() > 0,
          "gallery filename filter is visible, enabled, and has usable geometry");
    const int galleryToolbarBottom =
        gallerySearchEdit ? gallerySearchEdit->mapTo(&w, gallerySearchEdit->rect().bottomLeft()).y()
                          : -1;
    const int galleryTop = thumbnailPanel ? thumbnailPanel->mapTo(&w, QPoint(0, 0)).y() : -1;
    CHECK(galleryToolbarBottom >= 0 && galleryTop > galleryToolbarBottom,
          "gallery toolbar occupies space above the thumbnail panel");
    CHECK(viewModeCombo && viewModeCombo->isVisible() && viewModeCombo->isEnabled(),
          "gallery view mode control is visible and enabled");
    CHECK(advancedFilterToggle && advancedFilterToggle->isVisible() &&
              advancedFilterToggle->isEnabled(),
          "advanced gallery filter toggle is visible and enabled");
    w.resize(800, 500);
    pump(10);
    CHECK(thumbnailPanel && thumbnailPanel->isVisible() && thumbnailPanel->width() > 0 &&
              thumbnailPanel->height() > 0,
          "minimum-size Browse window keeps the gallery visible");
    w.resize(1280, 800);
    pump(10);

    auto *thumbnailSizeSlider = w.findChild<QSlider *>("thumbnailSizeSlider");
    auto *pathEdit = w.findChild<QLineEdit *>("pathEdit");
    CHECK(thumbnailSizeSlider != nullptr, "thumbnail size slider is discoverable");
    CHECK(viewModeCombo && thumbnailPanel,
          "thumbnail view controls expose a stable panel and combo");
    CHECK(pathEdit != nullptr, "directory path input is discoverable");
    auto *emptyState = w.findChild<QLabel *>("emptyStateLabel");
    auto *emptyFolder = w.findChild<QLabel *>("emptyFolderLabel");
    CHECK(emptyState != nullptr, "empty-state hint has a stable object name");
    CHECK(emptyFolder != nullptr, "empty-folder hint has a stable object name");
    CHECK(emptyState && emptyState->isVisible(),
          "empty-state hint is shown when no directory is open");
    CHECK(metadataAction && !metadataAction->isEnabled() && !metadataAction->isChecked(),
          "empty Browse disables the image-information command without a stale checked state");
    CHECK(zoomInAction && zoomOutAction && zoomFitAction && zoomActualAction &&
              !zoomInAction->isEnabled() && !zoomOutAction->isEnabled() &&
              !zoomFitAction->isEnabled() && !zoomActualAction->isEnabled(),
          "hidden Viewer disables all menu zoom commands");
    CHECK(slideshowAction && !slideshowAction->isEnabled(),
          "empty Browse disables slideshow until an image and directory are available");
    CHECK(historyBackAction && historyForwardAction && !historyBackAction->isEnabled() &&
              !historyForwardAction->isEnabled(),
          "empty Browse disables image history at both boundaries");
    CHECK(dirBackAction && dirForwardAction && !dirBackAction->isEnabled() &&
              !dirForwardAction->isEnabled(),
          "empty Browse disables directory history at both boundaries");
    CHECK(dirUpAction && !dirUpAction->isEnabled() && refreshAction && !refreshAction->isEnabled(),
          "empty Browse disables directory-only commands without an active folder");
    CHECK(addFavoriteAction && !addFavoriteAction->isEnabled() && removeFavoriteAction &&
              !removeFavoriteAction->isEnabled(),
          "empty Browse disables favorite commands without an active folder");
    CHECK(compareAction && !compareAction->isEnabled(),
          "empty Browse disables Compare until at least two images are available");

    if (thumbnailSizeSlider && viewModeCombo && thumbnailPanel)
    {
        thumbnailSizeSlider->setValue(180);
        CHECK(thumbnailPanel->thumbSize() == 180 && thumbnailSizeSlider->value() == 180,
              "standard grid remembers the user-selected 180px size");
        auto setViewModeFromCombo = [viewModeCombo](ThumbnailPanel::ViewMode mode)
        {
            for (int i = 0; i < viewModeCombo->count(); ++i)
                if (viewModeCombo->itemData(i).toInt() == static_cast<int>(mode))
                {
                    viewModeCombo->setCurrentIndex(i);
                    return;
                }
        };
        setViewModeFromCombo(ThumbnailPanel::LargeIcon);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon &&
                  thumbnailSizeSlider->value() == 240,
              "Large view synchronizes mode and slider at 240px");
        setViewModeFromCombo(ThumbnailPanel::SmallIcon);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::SmallIcon &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::SmallIcon &&
                  thumbnailSizeSlider->value() == 64,
              "Small view synchronizes mode and slider at 64px");
        setViewModeFromCombo(ThumbnailPanel::Thumbnail);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::Thumbnail &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::Thumbnail &&
                  thumbnailSizeSlider->value() == 180 && thumbnailPanel->thumbSize() == 180,
              "returning to Thumbnail restores the remembered 180px grid");

        setViewModeFromCombo(ThumbnailPanel::LargeIcon);
        thumbnailPanel->setThumbSize(192); // Restore/preferences call after the mode.
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                  thumbnailPanel->thumbSize() == 240 && thumbnailSizeSlider->value() == 240,
              "restore size cannot override the Large 240px preset");
        CHECK(!thumbnailSizeSlider->isEnabled(), "Large preset disables the thumbnail size slider");
        thumbnailPanel->setViewMode(ThumbnailPanel::LargeIcon);
        CHECK(viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon &&
                  thumbnailSizeSlider->value() == 240,
              "same-mode setViewMode re-synchronizes the effective Large size");
        setViewModeFromCombo(ThumbnailPanel::SmallIcon);
        CHECK(!thumbnailSizeSlider->isEnabled() && thumbnailSizeSlider->value() == 64,
              "Small preset disables the slider and keeps its 64px value");

        setViewModeFromCombo(ThumbnailPanel::Details);
        CHECK(!thumbnailSizeSlider->isEnabled(),
              "Details disables the thumbnail size slider because it has no visible effect");
        thumbnailPanel->setThumbSize(196); // Preferences call while Details is active.
        CHECK(thumbnailSizeSlider->value() == 196,
              "Details accepts a size for the future standard grid");
        setViewModeFromCombo(ThumbnailPanel::List);
        CHECK(!thumbnailSizeSlider->isEnabled(),
              "List disables the thumbnail size slider because it has no visible effect");
        setViewModeFromCombo(ThumbnailPanel::Details);
        setViewModeFromCombo(ThumbnailPanel::Thumbnail);
        CHECK(thumbnailPanel->thumbSize() == 196 && thumbnailSizeSlider->value() == 196,
              "Thumbnail restores the size remembered while Details was active");
        CHECK(thumbnailSizeSlider->isEnabled(), "Thumbnail re-enables the thumbnail size slider");
    }

    if (browseWorkspaceAction)
    {
        browseWorkspaceAction->setChecked(false);
        if (analysisAction && !analysisAction->isChecked())
            analysisAction->trigger();
        if (searchAction && searchAction->isChecked())
            searchAction->trigger();
        browseWorkspaceAction->setChecked(true);
        pump(10);
        CHECK(navigationPanel && navigationPanel->isVisible() && analysisPanel &&
                  !analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
                  browseWorkspaceAction->isChecked(),
              "browse layout keeps navigation and gives the gallery the available space");
        browseWorkspaceAction->trigger();
        pump(10);
        CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel &&
                  !searchPanel->isVisible() && browseWorkspaceAction &&
                  !browseWorkspaceAction->isChecked(),
              "leaving browse layout strictly restores the recorded panel state");
        browseWorkspaceAction->trigger();
        pump(10);
    }

    if (analysisAction)
        analysisAction->trigger();
    CHECK(analysisPanel && analysisPanel->isVisible() && analysisAction &&
              analysisAction->isChecked(),
          "analysis action and panel visibility stay synchronized");
    CHECK(searchPanel && !searchPanel->isVisible(), "search panel remains independently hidden");

    if (thumbnailSizeSlider)
        thumbnailSizeSlider->setFocus(Qt::OtherFocusReason);
    pump(10);
    CHECK(QApplication::focusWidget() == thumbnailSizeSlider,
          "focus can be placed on the real thumbnail size slider");
    sendFocusedKey(Qt::Key_Tab);
    CHECK(navigationPanel && !navigationPanel->isVisible() && analysisPanel &&
              !analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
              focusAction && focusAction->isChecked(),
          "plain Tab from a focused child enters focus browse");
    CHECK(analysisAction && !analysisAction->isEnabled() && searchAction &&
              !searchAction->isEnabled(),
          "focus browse disables panel toggles while their state is suspended");

    if (pathEdit)
        pathEdit->setFocus(Qt::OtherFocusReason);
    pump(10);
    CHECK(QApplication::focusWidget() == pathEdit,
          "focus can be placed on the real directory path input");
    sendFocusedKey(Qt::Key_Tab);
    CHECK(navigationPanel && navigationPanel->isVisible() && analysisPanel &&
              analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
              focusAction && !focusAction->isChecked(),
          "plain Tab from a focused path input exits focus browse");
    CHECK(analysisAction && analysisAction->isEnabled() && analysisAction->isChecked() &&
              searchAction && searchAction->isEnabled() && !searchAction->isChecked(),
          "leaving focus browse restores action state as well as panels");

    if (pathEdit)
        pathEdit->setFocus(Qt::OtherFocusReason);
    pump(10);
    sendFocusedKey(Qt::Key_Tab, Qt::ShiftModifier);
    CHECK(focusAction && !focusAction->isChecked(),
          "Shift+Tab keeps normal focus traversal and does not enter focus browse");
    if (analysisAction)
        analysisAction->trigger();

    auto *sel = w.findChild<SelectionModel *>();
    auto *dirModel = w.findChild<DirectoryModel *>();
    CHECK(sel != nullptr, "MainWindow exposes the SelectionModel SSOT");
    CHECK(dirModel != nullptr, "MainWindow exposes the DirectoryModel SSOT");
    CHECK(directoryTree != nullptr, "directory navigation tree is discoverable");
    if (!sel || !dirModel || !directoryTree)
        return;

    // ImageViewer 是独立的顶层窗口（MainWindow::setupUi 创建，onImageOpen 时 show）。
    // 整个测试进程只构造一次 MainWindow，setupUi 也只执行一次，因此这里抓到的
    // 一定是 onImageOpen 实际使用的那个 m_imageViewer（早期版本因重复 setupUi
    // 会泄漏第二个孤儿 viewer，导致抓取不确定而 flaky）。
    ImageViewer *viewer = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *v = qobject_cast<ImageViewer *>(top))
            viewer = v;
    CHECK(viewer != nullptr, "image viewer window exists after setupUi");

    // 模拟 main() 的真实启动顺序：窗口先构造完成，再设置命令行/文件关联路径。
    // 事件循环必须驱动 queued open，不能直接调用 onImageOpen 绕过回归点。
    w.setOpenOnLaunch(paths[0]);
    QElapsedTimer launchTimer;
    launchTimer.start();
    while (
        (sel->currentImage() != paths[0] || !viewer || !viewer->isVisible() || !viewer->frame()) &&
        launchTimer.elapsed() < 5000)
        pump(25);
    CHECK(sel->currentImage() == paths[0],
          "post-construction setOpenOnLaunch syncs SelectionModel.currentImage");
    CHECK(viewer && viewer->isVisible(),
          "post-construction setOpenOnLaunch makes ImageViewer visible");
    CHECK(viewer && (viewer->isFullScreen() ||
                     viewer->property("mviewerFullscreenRequested").toBool()),
          "Browse open requests fullscreen before the first viewer presentation");
    CHECK(viewer && viewer->frame(), "post-construction setOpenOnLaunch decodes a frame within 5s");
    CHECK(zoomInAction && zoomOutAction && zoomFitAction && zoomActualAction &&
              zoomInAction->isEnabled() && zoomOutAction->isEnabled() &&
              zoomFitAction->isEnabled() && zoomActualAction->isEnabled(),
          "visible Viewer enables all menu zoom commands");

    // 打开第一张图（真实产品入口 onImageOpen：双击文件 → 查看器）。
    // 注意顺序：先打开单图、等首屏，再打开目录。offscreen 测试平台不允许
    // 跨线程池并发全分辨率 QImageReader::read()（M3 已知平台限制，见
    // ImageRepository.cpp），因此测试按真实用户节奏逐步推进而不是并发轰炸。
    // Launch coverage above already opened the first image through the public
    // setter; do not call onImageOpen directly and hide a duplicate-open bug.
    pump(100);
    CHECK(sel->currentImage() == paths[0],
          "launch open keeps SelectionModel.currentImage synchronized");

    // Return in the editable path field must navigate without invoking the
    // window-level quick-preview command or reopening the old image.
    if (viewer)
    {
        sendKey(viewer, Qt::Key_Escape);
        pump(20);
        CHECK(!viewer->isVisible(), "Escape closes the default fullscreen Viewer in one step");
        CHECK(zoomInAction && zoomOutAction && zoomFitAction && zoomActualAction &&
                  !zoomInAction->isEnabled() && !zoomOutAction->isEnabled() &&
                  !zoomFitAction->isEnabled() && !zoomActualAction->isEnabled(),
              "closed Viewer disables menu zoom commands without clearing Browse selection");
        CHECK(sel->currentImage() == paths[0],
              "Escape returns to Browse without changing the current selection");
    }
    if (pathEdit)
    {
        pathEdit->setText(nextDir);
        pathEdit->setFocus(Qt::OtherFocusReason);
        sendFocusedKey(Qt::Key_Return);
        CHECK(viewer && !viewer->isVisible(),
              "Return with a path editor focused does not quick-preview the old image");
    }

    // Directory switch: clear stale SSOT selection synchronously, then select
    // the first new gallery item once the asynchronous scan publishes rows.
    sel->setCurrentImage(paths[0]);
    directoryTree->navigateTo(nextDir, true);
    CHECK(sel->currentImage().isEmpty(),
          "directory change clears the previous current image before scanning");
    QElapsedTimer firstSelectionTimer;
    firstSelectionTimer.start();
    while (sel->currentImage() != nextPath && firstSelectionTimer.elapsed() < 5000)
        pump(25);
    CHECK(sel->currentImage() == nextPath,
          "first item in a newly loaded directory is selected exactly through the SSOT");
    CHECK(emptyState && !emptyState->isVisible(),
          "empty-state hint hides once a directory is open");
    directoryTree->navigateTo(dirPath, true);
    pump(250);
    CHECK(dirBackAction && dirBackAction->isEnabled() && dirForwardAction &&
              !dirForwardAction->isEnabled(),
          "directory history enables Back after leaving the first folder");
    CHECK(dirUpAction && dirUpAction->isEnabled() && refreshAction && refreshAction->isEnabled(),
          "active Browse folder enables Up and Refresh");
    CHECK(addFavoriteAction && addFavoriteAction->isEnabled() && removeFavoriteAction &&
              !removeFavoriteAction->isEnabled(),
          "unfavorited active folder enables Add Favorite only");
    if (addFavoriteAction)
        addFavoriteAction->trigger();
    pump(20);
    CHECK(addFavoriteAction && !addFavoriteAction->isEnabled() && removeFavoriteAction &&
              removeFavoriteAction->isEnabled(),
          "adding the active folder immediately swaps favorite command availability");
    if (removeFavoriteAction)
        removeFavoriteAction->trigger();
    pump(20);
    CHECK(addFavoriteAction && addFavoriteAction->isEnabled() && removeFavoriteAction &&
              !removeFavoriteAction->isEnabled(),
          "removing the active folder restores Add Favorite availability");

    const QString externalDir = QDir(QStringLiteral(MVIEWER_SOURCE_DIR))
                                    .filePath(QStringLiteral("testdata/golden/256x256"));
    if (directoryTree->filterEdit())
        directoryTree->filterEdit()->setText(QStringLiteral("hide-target-until-navigation"));
    directoryTree->navigateTo(externalDir, true);
    QElapsedTimer externalNavigationTimer;
    externalNavigationTimer.start();
    while (directoryTree->currentPath() != QDir::cleanPath(externalDir) &&
           externalNavigationTimer.elapsed() < 5000)
        pump(25);
    CHECK(directoryTree->currentPath() == QDir::cleanPath(externalDir),
          "directory tree navigates to an existing path outside the home directory");
    CHECK(directoryTree->filterEdit() && directoryTree->filterEdit()->text().isEmpty(),
          "external navigation clears a filter that would hide the target");
    CHECK(directoryTree->currentIndex().isValid() &&
              directoryTree->currentIndex().data(Qt::DisplayRole).toString() == "256x256",
          "external directory navigation leaves the target visibly selected");

    // Empty-folder feedback: a directory without image files must show the
    // no-images hint, and it must disappear once a real folder is opened.
    const QString emptyDir = sourceDir.filePath(QStringLiteral("wf_empty"));
    QDir().mkpath(emptyDir);
    directoryTree->navigateTo(emptyDir, true);
    QElapsedTimer emptyDirTimer;
    emptyDirTimer.start();
    while ((!emptyFolder || !emptyFolder->isVisible()) && emptyDirTimer.elapsed() < 5000)
        pump(25);
    CHECK(emptyFolder && emptyFolder->isVisible(), "empty folder shows the no-images hint");
    directoryTree->navigateTo(dirPath, true);
    pump(300);
    CHECK(emptyFolder && !emptyFolder->isVisible(),
          "no-images hint hides once a folder with images is opened");

    // The F1 cheat sheet must stay in sync with the registered keyboard
    // commands (regression: Ctrl+F directory filter and F1 help were
    // missing from the sheet).
    const QString helpHtml = MainWindow::shortcutsHelpHtml();
    CHECK(helpHtml.contains("Ctrl+F"),
          "cheat sheet documents the Ctrl+F directory-filter shortcut");
    CHECK(helpHtml.contains("F1"), "cheat sheet documents the F1 help shortcut");
    if (viewer)
    {
        // 首屏：异步解码必须在 5 秒内交付（评审"有没有等待"硬上限）。
        QElapsedTimer t;
        t.start();
        while (!viewer->frame() && t.elapsed() < 5000)
            pump(25);
        CHECK(viewer->frame() != nullptr, "first image decodes within 5s");
    }

    // 打开目录（真实 SSOT 路径：DirectoryModel 广播给缩略图/面包屑等），
    // 并等缩略图管线排空后再继续键盘浏览。
    dirModel->setCurrentDirectory(dirPath);
    pump(100);
    CHECK(dirModel->currentDirectory() == dirPath, "open directory lands in DirectoryModel");
    pump(400); // 5 张 32x32 缩略图解码窗口

    auto *imageList = w.findChild<ImageListModel *>();
    CHECK(imageList && thumbnailPanel && imageList->paths() == thumbnailPanel->pathList(),
          "ImageListModel publishes the same ordered sequence as the gallery");
    CHECK(viewer && imageList && viewer->browseSequence() == imageList->paths(),
          "Viewer consumes the Browse sequence SSOT for neighbors and position");

    // Regression: opening a distant thumbnail through the real QListView
    // double-click path must activate the standalone Viewer and give it the
    // keyboard focus. Otherwise the image appears to stop responding and ESC
    // is delivered to the gallery instead of closing the Viewer.
    if (thumbnailPanel && viewer && thumbnailPanel->pathList().size() >= 2)
    {
        viewer->hide();
        thumbnailPanel->selectPath(thumbnailPanel->pathList().last());
        pump(50);
        const QModelIndex index = thumbnailPanel->model()->index(
            thumbnailPanel->pathList().size() - 1, 0);
        const QRect itemRect = thumbnailPanel->visualRect(index);
        CHECK(itemRect.isValid() && itemRect.width() > 0 && itemRect.height() > 0,
              "distant thumbnail remains addressable after scrolling into view");
        if (itemRect.isValid() && itemRect.width() > 0 && itemRect.height() > 0)
        {
            const QPoint point = itemRect.center();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(thumbnailPanel->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(thumbnailPanel->viewport(), &release);
            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(point),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(thumbnailPanel->viewport(), &doubleClick);
            QElapsedTimer openTimer;
            openTimer.start();
            while ((!viewer->isVisible() || QApplication::focusWidget() != viewer) &&
                   openTimer.elapsed() < 1000)
                pump(25);
            CHECK(viewer->isVisible(), "real thumbnail double-click opens the Viewer");
            CHECK(QApplication::focusWidget() == viewer,
                  "real thumbnail double-click transfers keyboard focus to the Viewer");
            sendFocusedKey(Qt::Key_Escape);
            CHECK(!viewer->isVisible(),
                  "ESC closes a Viewer opened by a real distant-thumbnail double-click");

            // Continue the canonical Browse workflow with the Viewer open.
            // Closing above intentionally cancels an in-flight decode; on a
            // slower CI runner the frame may therefore still be null. Reopen
            // through the same real double-click path before keyboard
            // navigation/zoom assertions instead of depending on decode speed.
            QMouseEvent reopen(QEvent::MouseButtonDblClick, QPointF(point),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(thumbnailPanel->viewport(), &reopen);
            QElapsedTimer reopenTimer;
            reopenTimer.start();
            while ((!viewer->isVisible() || !viewer->frame()) &&
                   reopenTimer.elapsed() < 5000)
                pump(25);
            CHECK(viewer->isVisible() && viewer->frame(),
                  "Browse workflow reopens a decoded Viewer after the ESC regression check");
        }
    }

    // M49: exercise the real QListView ExtendedSelection gesture path rather
    // than publishing a synthetic SelectionModel value. Plain, Ctrl-toggle,
    // Shift-range, and Ctrl-toggle-off must all keep the gallery and SSOT in
    // agreement.
    if (thumbnailPanel && thumbnailPanel->pathList().size() >= 5 && sel)
    {
        auto galleryClick = [thumbnailPanel](int row, Qt::KeyboardModifiers modifiers)
        {
            const QModelIndex index = thumbnailPanel->model()->index(row, 0);
            const QPoint point = thumbnailPanel->visualRect(index).center();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton,
                              Qt::LeftButton, modifiers);
            QApplication::sendEvent(thumbnailPanel->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), Qt::LeftButton,
                                Qt::NoButton, modifiers);
            QApplication::sendEvent(thumbnailPanel->viewport(), &release);
            pump(30);
        };
        galleryClick(0, Qt::NoModifier);
        CHECK(thumbnailPanel->selectedPaths().size() == 1 &&
                  thumbnailPanel->selectedPaths().contains(paths[0]) && sel->selection().size() == 1,
              "M49: plain gallery click selects one image through the real view");
        galleryClick(2, Qt::ControlModifier);
        CHECK(thumbnailPanel->selectedPaths().size() == 2 &&
                  thumbnailPanel->selectedPaths().contains(paths[0]) &&
                  thumbnailPanel->selectedPaths().contains(paths[2]) && sel->selection().size() == 2,
              "M49: Ctrl+click toggles a second image into the SSOT selection");
        galleryClick(4, Qt::ShiftModifier);
        CHECK(thumbnailPanel->selectedPaths().size() >= 4 && sel->selection().size() >= 4 &&
                  thumbnailPanel->selectedPaths().contains(paths[3]) &&
                  thumbnailPanel->selectedPaths().contains(paths[4]),
              "M49: Shift+click selects the native contiguous range");
        galleryClick(2, Qt::ControlModifier);
        CHECK(!thumbnailPanel->selectedPaths().contains(paths[2]) &&
                  !sel->selection().contains(paths[2]) && sel->selection().size() >= 3,
              "M49: Ctrl+click removes one item without collapsing the range");
    }

    // Sort/filter rebuilds must republish the order that is actually visible;
    // navigation consumers must never fall back to a filename scan.
    thumbnailPanel->setSortMode(ThumbnailPanel::SortName);
    thumbnailPanel->setSortAscending(false);
    pump(50);
    const QStringList descending = imageList ? imageList->paths() : QStringList();
    CHECK(imageList && thumbnailPanel && descending == thumbnailPanel->pathList(),
          "descending gallery sort republishes the visible Browse sequence");
    CHECK(viewer && imageList && viewer->browseSequence() == descending,
          "Viewer sequence follows descending gallery sort");
    if (thumbnailPanel)
    {
        thumbnailPanel->selectPath(paths.first());
        pump(20);
    }
    thumbnailPanel->setFilter(QFileInfo(paths[1]).completeBaseName());
    pump(50);
    CHECK(imageList && thumbnailPanel && imageList->paths() == thumbnailPanel->pathList() &&
              imageList->paths() == QStringList{paths[1]},
          "filename filter republishes the filtered Browse sequence");
    CHECK(thumbnailPanel && thumbnailPanel->selectedPaths() == QStringList{paths[1]},
          "filename filter promotes the first visible image into the gallery selection");
    CHECK(sel && sel->currentImage() == paths[1],
          "filename filter keeps SelectionModel.currentImage on the visible image");
    CHECK(previewPanel && previewPanel->requestedPath() == paths[1],
          "filename filter routes the visible image to the PreviewPanel");
    thumbnailPanel->setFilter(QStringLiteral("__mviewer_filter_no_match__"));
    pump(50);
    CHECK(imageList && thumbnailPanel && imageList->paths().isEmpty() &&
              thumbnailPanel->pathList().isEmpty(),
          "a no-match filter publishes an empty visible Browse sequence");
    CHECK(thumbnailPanel && thumbnailPanel->selectedPaths().isEmpty(),
          "a no-match filter clears the gallery selection");
    CHECK(sel && sel->currentImage().isEmpty(),
          "a no-match filter clears SelectionModel.currentImage");
    CHECK(previewPanel && previewPanel->requestedPath().isEmpty(),
          "a no-match filter clears the PreviewPanel request");
    const QString browseTitle = QString("%1 - MViewer").arg(QDir(dirPath).dirName());
    CHECK(w.windowTitle() == browseTitle,
          "a no-match filter changes the title to the active directory");
    thumbnailPanel->setFilter({});
    thumbnailPanel->setSortAscending(true);
    pump(50);
    CHECK(sel && sel->currentImage().isEmpty() && w.windowTitle() == browseTitle,
          "clearing an empty filter keeps the directory title without a selection");
    if (sel)
        sel->setCurrentImage(paths.first());
    pump(20);
    CHECK(w.windowTitle() == QString("%1 - MViewer").arg(QFileInfo(paths.first()).fileName()),
          "selecting an image restores its filename title");

    // 键盘浏览：→ → ← （评审"滚轮切换"的键盘等价路径 navigate()）。
    sendKey(&w, Qt::Key_Right);
    CHECK(sel->currentImage() == paths[1], "Right arrow advances to image #2");
    sendKey(&w, Qt::Key_Right);
    CHECK(sel->currentImage() == paths[2], "Right arrow advances to image #3");
    sendKey(&w, Qt::Key_Left);
    CHECK(sel->currentImage() == paths[1], "Left arrow goes back to image #2");

    // Home / End / PageUp / PageDown 边界（beta_checklist "浏览体验"）。
    sendKey(&w, Qt::Key_End);
    CHECK(sel->currentImage() == paths.last(), "End jumps to the last image");
    sendKey(&w, Qt::Key_Home);
    CHECK(sel->currentImage() == paths.first(), "Home jumps to the first image");
    sendKey(&w, Qt::Key_PageUp); // 已在第一张：必须不越界、不崩溃
    CHECK(sel->currentImage() == paths.first(), "PageUp at first image does not underflow");
    sendKey(&w, Qt::Key_PageDown);
    CHECK(paths.contains(sel->currentImage()), "PageDown stays within the folder");

    // 快速切图后视图不能卡在空白/旧帧（评审"快速切图"稳定性检查）。
    if (viewer)
    {
        QElapsedTimer t;
        t.start();
        while (!viewer->frame() && t.elapsed() < 5000)
            pump(25);
        CHECK(viewer->frame() != nullptr,
              "after rapid keyboard navigation the viewer still shows a frame");
    }

    // 缩放循环：Fit → 100% → 放大 → Fit（评审"双击放大 → 恢复"的核心状态机）。
    if (viewer)
    {
        viewer->resize(800, 600);
        pump(30);

        if (viewer->frame())
        {
            viewer->zoomFit();
            pump(20);
            const double fitScale = viewer->viewTransform().scale;
            CHECK(fitScale > 0.0, "Fit produces a positive scale");

            viewer->zoomActual();
            pump(20);
            CHECK(qAbs(viewer->viewTransform().scale - 1.0) < 1e-9,
                  "100% (zoomActual) sets scale to exactly 1.0");

            viewer->zoomIn();
            pump(20);
            CHECK(viewer->viewTransform().scale > 1.0, "zoomIn grows the scale beyond 100%");

            viewer->zoomFit();
            pump(20);
            CHECK(qAbs(viewer->viewTransform().scale - fitScale) < 1e-6,
                  "restore (Fit) returns to the original fit scale");

            // Wheel zoom keeps the image point under the cursor stationary
            // (beta checklist “缩放中心：以鼠标点为中心”).
            {
                viewer->zoomFit();
                pump(20);
                const auto before = viewer->viewTransform();
                const QPointF cursor(220.0, 160.0);
                const double imgX = (cursor.x() - before.offsetX) / before.scale;
                const double imgY = (cursor.y() - before.offsetY) / before.scale;
                QWheelEvent wheel(cursor, viewer->mapToGlobal(cursor.toPoint()), QPoint(0, 0),
                                  QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                                  false);
                QApplication::sendEvent(viewer, &wheel);
                pump(20);
                const auto after = viewer->viewTransform();
                CHECK(after.scale > before.scale, "wheel zoom-in grows the scale");
                const double drift = qAbs((cursor.x() - after.offsetX) / after.scale - imgX) +
                                     qAbs((cursor.y() - after.offsetY) / after.scale - imgY);
                CHECK(drift < 0.5, "wheel zoom keeps the image point under the cursor fixed");
            }

            // Ctrl+C copies the current image to the clipboard. It must NEVER
            // open the file-copy dialog (the context menu no longer advertises
            // Ctrl+C for that action — same key, different behavior).
            QApplication::clipboard()->clear();
            sendKey(&w, Qt::Key_C, Qt::ControlModifier);
            // Copy is intentionally an asynchronous worker export. A short
            // fixed pump passes on a fast desktop but races the worker on CI;
            // wait for the observable clipboard result instead.
            QElapsedTimer clipboardTimer;
            clipboardTimer.start();
            while (QApplication::clipboard()->image().isNull() &&
                   clipboardTimer.elapsed() < 5000)
                pump(25);
            CHECK(!QApplication::clipboard()->image().isNull(),
                  "Ctrl+C copies the current image to the clipboard");

            // Slideshow (S): starts a timer that advances through the folder;
            // S again stops it. Settings drive the interval (min 500 ms).
            {
                QSettings slideshowSettings;
                slideshowSettings.setValue("slideshowInterval", 500);
                slideshowSettings.sync();
                const QString beforeSlideshow = sel->currentImage();
                sendKey(&w, Qt::Key_S);
                pump(1200);
                const QString afterAdvance = sel->currentImage();
                CHECK(!afterAdvance.isEmpty() && afterAdvance != beforeSlideshow,
                      "S starts slideshow and advances to the next image");
                sendKey(&w, Qt::Key_S);
                pump(700);
                CHECK(sel->currentImage() == afterAdvance,
                      "S again stops slideshow (no further advance)");
            }

            // Double-click toggles fit <-> 100% at the cursor (beta
            // checklist "双击放大 -> 恢复").
            {
                viewer->zoomFit();
                pump(20);
                const double fitScale = viewer->viewTransform().scale;
                const QPointF dcPos(300.0, 200.0);
                QMouseEvent dbl(QEvent::MouseButtonDblClick, dcPos,
                                viewer->mapToGlobal(dcPos.toPoint()), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &dbl);
                pump(20);
                CHECK(qAbs(viewer->viewTransform().scale - 1.0) < 1e-9,
                      "double-click zooms to 100%");
                QApplication::sendEvent(viewer, &dbl);
                pump(20);
                CHECK(qAbs(viewer->viewTransform().scale - fitScale) < 1e-6,
                      "second double-click restores Fit");
            }

            // Mouse side buttons navigate prev/next (cheat sheet
            // "← / → / 鼠标侧键").
            {
                const QString beforeNav = sel->currentImage();
                QMouseEvent back(QEvent::MouseButtonPress, QPointF(10, 10),
                                 viewer->mapToGlobal(QPoint(10, 10)), Qt::BackButton,
                                 Qt::BackButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &back);
                pump(100);
                CHECK(!sel->currentImage().isEmpty() && sel->currentImage() != beforeNav,
                      "mouse back button navigates to the previous image");
                QMouseEvent fwd(QEvent::MouseButtonPress, QPointF(10, 10),
                                viewer->mapToGlobal(QPoint(10, 10)), Qt::ForwardButton,
                                Qt::ForwardButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &fwd);
                pump(100);
                CHECK(sel->currentImage() == beforeNav,
                      "mouse forward button navigates back to the original image");
            }

            // I toggles the metadata overlay for the current image; ESC
            // hides it (cheat sheet “I/M → 图片信息浮层，ESC 关闭”).
            {
                // The viewer (and its overlay) are a top-level window.
                MetadataOverlay *overlay = nullptr;
                for (QWidget *top : QApplication::topLevelWidgets())
                {
                    overlay = top->findChild<MetadataOverlay *>();
                    if (overlay)
                        break;
                }
                CHECK(overlay != nullptr, "metadata overlay exists");
                if (overlay && !overlay->isVisible())
                {
                    sendKey(&w, Qt::Key_I);
                    pump(50);
                    CHECK(overlay->isVisible(), "I shows the metadata overlay");
                }
                sendKey(&w, Qt::Key_Escape);
                pump(50);
                CHECK(overlay && !overlay->isVisible(), "ESC hides the metadata overlay");
            }

            // M-viewer P0-3 async metadata-overlay histogram contract: showing
            // the overlay schedules exactly ONE Analysis batch that arrives
            // asynchronously (never blocking the toggle), a current-image change
            // clears the old histogram IMMEDIATELY through the synchronous SSOT
            // signals, and a fresh histogram for the new dimensions replaces it.
            // A release-gated blocker pins the single Analysis worker so the
            // queued batch and the immediate clear are observable.
            {
                auto *toggleAction = w.findChild<QAction *>("toggleMetadataAction");
                MetadataOverlay *overlay =
                    viewer ? viewer->findChild<MetadataOverlay *>() : nullptr;
                HistogramWidget *hist = overlay ? overlay->findChild<HistogramWidget *>() : nullptr;
                CHECK(toggleAction != nullptr, "metadata toggle action is discoverable");
                CHECK(overlay != nullptr, "metadata overlay is discoverable on the viewer");
                CHECK(hist != nullptr, "metadata overlay owns a histogram widget");

                auto &sched = TaskScheduler::instance();
                const QString imgA = sel->currentImage();
                {
                    QElapsedTimer syncFrame;
                    syncFrame.start();
                    while ((!viewer->frame() ||
                            QString::fromStdString(viewer->frame()->metadata().filePath) != imgA) &&
                           syncFrame.elapsed() < 8000)
                        pump(25);
                }
                waitForAnalysisIdle();
                sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
                struct RestoreAnalysisThreads
                {
                    ~RestoreAnalysisThreads()
                    {
                        TaskScheduler::instance().setQueueMaxThreads(
                            TaskScheduler::Priority::Analysis,
                            std::max(1, QThread::idealThreadCount() / 2));
                    }
                } restoreAnalysis;

                if (toggleAction && overlay && hist)
                {
                    const int64_t srcPixels =
                        viewer->frame()
                            ? static_cast<int64_t>(viewer->frame()->width()) *
                                  viewer->frame()->height()
                            : 0;
                    {
                        AnalysisBlocker blocker;
                        {
                            QElapsedTimer t;
                            t.start();
                            while (sched.metrics(TaskScheduler::PoolType::AnalysisPool)
                                       .active_tasks < 1 &&
                                   t.elapsed() < 5000)
                                pump(10);
                        }
                        CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool)
                                  .active_tasks >= 1,
                              "metadata gate: blocker occupies the single Analysis worker");

                        const uint64_t submittedBefore =
                            sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                        {
                            QElapsedTimer prompt;
                            prompt.start();
                            toggleAction->trigger();
                            pump(20);
                            CHECK(prompt.elapsed() < 2000,
                                  "metadata gate: toggle returns without sync histogram compute");
                        }
                        const uint64_t submittedAfter =
                            sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                        CHECK(submittedAfter == submittedBefore + 1,
                              "metadata gate: showing schedules exactly one Analysis batch");
                        CHECK(overlay->isVisible(),
                              "metadata gate: overlay is visible after the toggle");
                        CHECK(toggleAction->isChecked(),
                              "metadata gate: toggle action is checked with the overlay");
                        CHECK(hist->histogramCount() == 0,
                              "metadata gate: no histogram while the Analysis worker is blocked");
                    }

                    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                                      std::chrono::seconds(15)),
                          "metadata gate: Analysis pool drains after the blocker release");
                    {
                        QElapsedTimer t;
                        t.start();
                        while (hist->histogramCount() == 0 && t.elapsed() < 8000)
                            pump(25);
                    }
                    CHECK(hist->histogramCount() == 1,
                          "metadata gate: async histogram arrives for the current image");
                    const long srcTotal = hist->histogramTotal(0);
                    CHECK(srcTotal == srcPixels,
                          "metadata gate: histogram total equals the frame pixel count");

                    const QString target = (imgA == paths[4]) ? paths[0] : paths[4];
                    CHECK(target != imgA, "metadata gate: target image has different dimensions");

                    const uint64_t submittedBefore2 =
                        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                    sel->setCurrentImage(target);
                    CHECK(hist->histogramCount() == 0,
                          "metadata gate: image change clears the old histogram immediately");
                    CHECK(overlay->isVisible(),
                          "metadata gate: overlay stays visible across the image change");
                    {
                        QElapsedTimer t;
                        t.start();
                        while ((!viewer->frame() ||
                                QString::fromStdString(viewer->frame()->metadata().filePath) !=
                                    target) &&
                               t.elapsed() < 8000)
                            pump(25);
                    }
                    {
                        QElapsedTimer t;
                        t.start();
                        while (hist->histogramCount() == 0 && t.elapsed() < 8000)
                            pump(25);
                    }
                    const uint64_t submittedAfter2 =
                        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                    CHECK(submittedAfter2 == submittedBefore2 + 1,
                          "metadata gate: the new image schedules exactly one fresh batch");
                    const long targetTotal = hist->histogramTotal(0);
                    const int64_t targetPixels =
                        viewer->frame()
                            ? static_cast<int64_t>(viewer->frame()->width()) *
                                  viewer->frame()->height()
                            : 0;
                    CHECK(targetTotal == targetPixels && targetTotal != srcTotal,
                          "metadata gate: fresh histogram has the new dimensions");

                    sendKey(&w, Qt::Key_Escape);
                    pump(50);
                    CHECK(!overlay->isVisible(), "metadata gate: ESC hides the overlay");
                    sel->setCurrentImage(imgA);
                    {
                        QElapsedTimer t;
                        t.start();
                        while ((!viewer->frame() ||
                                QString::fromStdString(viewer->frame()->metadata().filePath) !=
                                    imgA) &&
                               t.elapsed() < 8000)
                            pump(25);
                    }
                    waitForAnalysisIdle();
                }
            }

            // F toggles fullscreen: the visible viewer, or the main window
            // when the viewer is hidden (cheat sheet “F / F11 → 全屏切换”).
            {
                // The offscreen QPA used by headless CI does not always expose
                // the native WindowFullScreen bit through QWidget::isFullScreen().
                // The requested property is the production fullscreen contract;
                // native state remains a best-effort side effect on real screens.
                const auto viewerFullscreenRequested = [&]()
                {
                    return viewer->property("mviewerFullscreenRequested").toBool();
                };
                if (viewer->isFullScreen())
                {
                    sendKey(&w, Qt::Key_F);
                    pump(20);
                }
                CHECK(!viewerFullscreenRequested() && !viewer->isFullScreen(),
                      "F exits the default Browse fullscreen state");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(viewerFullscreenRequested() || viewer->isFullScreen(),
                      "F fullscreens the visible viewer");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(!viewerFullscreenRequested() && !viewer->isFullScreen(),
                      "F again exits fullscreen");

                viewer->hide();
                pump(20);
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(w.isFullScreen(), "F fullscreens the main window when the viewer is hidden");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(!w.isFullScreen(), "F again exits the main-window fullscreen");
                viewer->show();
                pump(20);
            }
        }
        viewer->close();
        pump(20);
    }

    // Compare requires 2-8 images: a single-image folder must not open a
    // degenerate one-pane compare dialog (user gets feedback instead).
    {
        const QString singleDir = sourceDir.filePath(QStringLiteral("wf_single"));
        QDir().mkpath(singleDir);
        const QString singlePath =
            writePng(QDir(singleDir), QStringLiteral("only.png"), QColor(200, 100, 50));
        directoryTree->navigateTo(singleDir, true);
        pump(200);
        sel->setCurrentImage(singlePath);
        pump(20);
        CHECK(metadataAction && metadataAction->isEnabled(),
              "selecting an image enables the image-information command");
        CHECK(slideshowAction && slideshowAction->isEnabled(),
              "selecting an image in a directory enables slideshow");
        CHECK(compareAction && !compareAction->isEnabled(),
              "a single-image folder keeps the impossible Compare command disabled");
        w.statusBar()->clearMessage();
        sendKey(&w, Qt::Key_C);
        pump(100);
        CHECK(w.statusBar()->currentMessage().contains(
                  QStringLiteral("需要至少两张图片才能比较")),
              "Compare keyboard command explains why a single-image folder cannot compare");
        bool compareOpened = false;
        for (QWidget *top : QApplication::topLevelWidgets())
        {
            auto *dlg = qobject_cast<QDialog *>(top);
            if (dlg && dlg->isVisible() && dlg->windowTitle().contains("比较模式"))
            {
                compareOpened = true;
                break;
            }
        }
        CHECK(!compareOpened, "Compare does not open with fewer than two images");
        directoryTree->navigateTo(dirPath, true);
        pump(200);
    }

    // Global search end-to-end: query a filename fragment, get results,
    // and double-click a row to open the image (shipped feature, full chain).
    {
        if (searchAction)
            searchAction->trigger();
        pump(50);
        auto *searchPanelWidget = w.findChild<SearchPanel *>();
        auto *searchEdit =
            searchPanelWidget ? searchPanelWidget->findChild<QLineEdit *>() : nullptr;
        auto *resultTable =
            searchPanelWidget ? searchPanelWidget->findChild<QTableWidget *>() : nullptr;
        CHECK(searchPanelWidget != nullptr, "global search panel is discoverable");
        CHECK(searchEdit != nullptr, "search panel exposes a query input");
        CHECK(resultTable != nullptr, "search panel exposes a results table");
        if (searchEdit && resultTable)
        {
            searchEdit->setText(QStringLiteral("wf_001"));
            QElapsedTimer searchTimer;
            searchTimer.start();
            while (resultTable->rowCount() == 0 && searchTimer.elapsed() < 10000)
                pump(50);
            CHECK(resultTable->rowCount() >= 1, "search returns a result for a filename fragment");
            bool named = false;
            for (int r = 0; r < resultTable->rowCount(); ++r)
                if (resultTable->item(r, 1) && resultTable->item(r, 1)->text().contains("wf_001"))
                    named = true;
            CHECK(named, "search result names the matched file");
            if (resultTable->rowCount() > 0)
            {
                resultTable->doubleClicked(resultTable->model()->index(0, 0));
                pump(300);
                CHECK(sel->currentImage().contains("wf_001"),
                      "double-clicking a search result opens the image");
            }
            searchEdit->clear();
        }
        if (searchAction)
            searchAction->trigger(); // hide the panel again
        pump(50);
    }

    // M25: closing while the BROWSE WORKSPACE is active must behave like the
    // focus-mode case — persist the state from before Browse hid the panels,
    // not the temporary hidden Browse layout. Exercised FIRST (window open),
    // then the window is re-shown for the existing focus-mode regression.
    if (analysisAction)
    {
        analysisAction->setChecked(false);
        analysisAction->trigger();
    }
    if (searchAction)
    {
        searchAction->setChecked(false);
        searchAction->trigger();
    }
    pump(10);
    CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel && searchPanel->isVisible(),
          "analysis + search panels are visible before the close-in-browse regression");
    if (browseWorkspaceAction)
        browseWorkspaceAction->trigger(); // enter Browse: panels hide temporarily
    pump(10);
    CHECK(browseWorkspaceAction && browseWorkspaceAction->isChecked() && analysisPanel &&
              !analysisPanel->isVisible(),
          "browse workspace hides the panels before close persistence is exercised");
    w.close();
    pump(50);
    const AppState persistedAfterBrowse = AppState::load();
    QSettings persistedSettings2;
    CHECK(persistedAfterBrowse.analysisVisible,
          "closing inside the Browse workspace persists the pre-Browse analysis visibility");
    CHECK(persistedSettings2.value("searchVisible", false).toBool(),
          "closing inside the Browse workspace persists the pre-Browse search visibility");
    w.show();
    pump(50);
    if (browseWorkspaceAction)
        browseWorkspaceAction->trigger(); // exit Browse: panels restored
    pump(10);

    // Closing while Focus Browse is active must persist the panel state from
    // before Focus hid the panels, not the temporary hidden state.
    if (analysisAction)
    {
        analysisAction->setChecked(false);
        analysisAction->trigger();
    }
    if (searchAction)
    {
        searchAction->setChecked(false);
        searchAction->trigger();
    }
    CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel && searchPanel->isVisible(),
          "analysis panel is visible before the close-in-focus regression");
    if (focusAction)
        focusAction->trigger();
    pump(10);
    CHECK(focusAction && focusAction->isChecked() && analysisPanel && !analysisPanel->isVisible(),
          "focus mode hides the panel before close persistence is exercised");
    w.close();
    pump(50);
    const AppState persisted = AppState::load();
    QSettings persistedSettings;
    CHECK(persisted.analysisVisible,
          "closing in focus mode persists the pre-focus analysis visibility");
    CHECK(persistedSettings.value("searchVisible", false).toBool(),
          "closing in focus mode persists the pre-focus search visibility");
    QFile::remove(nextPath);
    QDir().rmdir(nextDir);
    pump(50);
    CHECK(!w.isVisible(), "main window closes cleanly after the browse workflow");
}

void workflow3_session_restore(const QString &dirPath, const QString &imagePath)
{
    std::cout << "—— Workflow 3: session restore ——\n";
    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    QFile::remove(recoveryPath);
    QSettings().clear();

    AppState expected;
    expected.lastDir = dirPath;
    expected.lastImage = imagePath;
    expected.analysisVisible = true;
    CHECK(expected.save(), "cross-restart fixture saves Analysis visibility");
    QSettings settings;
    settings.setValue("searchVisible", true);
    // Reproduce an older persisted session: LargeIcon is a fixed 240px preset,
    // while the old slider value may still contain the previous 180px grid size.
    settings.setValue("thumbViewMode", static_cast<int>(ThumbnailPanel::LargeIcon));
    settings.setValue("thumbSize", 180);
    settings.sync();

    {
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        pump(350);
        auto *analysisPanel = w.findChild<QWidget *>("analysisPanel");
        auto *searchPanel = w.findChild<QWidget *>("searchPanel");
        auto *analysisAction = w.findChild<QAction *>("toggleAnalysisPanelAction");
        auto *searchAction = w.findChild<QAction *>("toggleSearchPanelAction");
        auto *browseAction = w.findChild<QAction *>("browseWorkspaceAction");
        auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
        auto *thumbnailSizeSlider = w.findChild<QSlider *>("thumbnailSizeSlider");
        auto *viewModeCombo = w.findChild<QComboBox *>("thumbnailViewModeCombo");
        CHECK(analysisPanel && analysisPanel->isVisible() && analysisAction &&
                  analysisAction->isChecked(),
              "Analysis panel and action restore together across a new MainWindow");
        CHECK(searchPanel && searchPanel->isVisible() && searchAction && searchAction->isChecked(),
              "Search panel and action restore together across a new MainWindow");
        CHECK(browseAction && !browseAction->isChecked(),
              "restored side panels keep the Browser workspace action unchecked");
        CHECK(
            thumbnailPanel && thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                thumbnailPanel->thumbSize() == 240 && thumbnailSizeSlider &&
                thumbnailSizeSlider->value() == 240 && !thumbnailSizeSlider->isEnabled() &&
                viewModeCombo && viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon,
            "deferred restore keeps the Large preset at 240px with a disabled synchronized slider");
        w.close();
        pump(50);
    }

    QFile::remove(recoveryPath);
    QSettings().clear();
    const QString configPath = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    if (!configPath.isEmpty())
        QDir(configPath).removeRecursively();
}

// ─── Workflow 2: 两图 Compare → 模式切换 → 互斥 → 退出 → 继续浏览 ───────────
void workflow2_compare(const QString &pathA, const QString &pathB)
{
    std::cout << "── Workflow 2: compare ──\n";

    SelectionModel sel;

    // 真实退出路径：CompareWorkspace 宿主是 QDialog，Esc → reject()。
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({pathA, pathB});
    dlg.resize(1100, 750);
    dlg.show();
    // M28 P1-01: compare loads are async; wait for both frames deterministically.
    {
        QElapsedTimer cmpLoad;
        cmpLoad.start();
        while (ws->comparedImageCount() != 2 && cmpLoad.elapsed() < 5000)
            pump(25);
    }

    QWidget *modeToolbar = ws->findChild<QWidget *>("compareModeToolbar");
    QWidget *viewToolbar = ws->findChild<QWidget *>("compareViewToolbar");
    QWidget *toolToolbar = ws->findChild<QWidget *>("compareToolToolbar");
    const int availableToolbarWidth = ws->contentsRect().width();
    CHECK(modeToolbar && viewToolbar && toolToolbar && modeToolbar->isVisible() &&
              viewToolbar->isVisible() && toolToolbar->isVisible(),
          "compare exposes three visible semantic toolbars");
    CHECK(modeToolbar && modeToolbar->width() <= availableToolbarWidth &&
              modeToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare mode toolbar fits the standard 1100px workspace width");
    CHECK(viewToolbar && viewToolbar->width() <= availableToolbarWidth &&
              viewToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare view toolbar fits the standard 1100px workspace width");
    CHECK(toolToolbar && toolToolbar->width() <= availableToolbarWidth &&
              toolToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare tool toolbar fits the standard 1100px workspace width");

    // 默认状态（评审"默认值不合理"检查）。
    CHECK(ws->comparedImageCount() == 2, "two images loaded into Compare");
    CHECK(ws->isSyncEnabled(), "sync zoom/pan is ON by default");
    CHECK(sel.compared().size() == 2, "SelectionModel.compared mirrors the compare set (SSOT)");
    CHECK(!sel.focused().isEmpty(), "a default focus/reference image is set");

    QCheckBox *blink = findChk(ws, QStringLiteral("闪烁对比"));
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *checker = findChk(ws, QStringLiteral("棋盘"));
    QPushButton *analyzeButton =
        ws->findChild<QPushButton *>(QStringLiteral("analyzeCompareButton"));
    QPushButton *exportReportButton =
        ws->findChild<QPushButton *>(QStringLiteral("exportCompareReportButton"));
    CHECK(blink && split && overlay, "mode checkboxes exist (blink/split/overlay)");
    CHECK(blink && split && swipe && overlay && checker && blink->isVisible() &&
              split->isVisible() && swipe->isVisible() && overlay->isVisible() &&
              checker->isVisible(),
          "all key compare mode controls remain visible at 1100x750");
    if (!blink || !split || !overlay)
        return;
    CHECK(split->isEnabled() && overlay->isEnabled(),
          "2-image modes are enabled for exactly two images");
    CHECK(analyzeButton && exportReportButton && blink->isEnabled() && analyzeButton->isEnabled() &&
              exportReportButton->isEnabled(),
          "loaded two-image Compare enables image-dependent actions");

    // 闪烁（B）开 → 关。
    sendKey(ws, Qt::Key_B);
    CHECK(blink->isChecked(), "key B turns blink compare ON");
    sendKey(ws, Qt::Key_B);
    CHECK(!blink->isChecked(), "key B turns blink compare OFF again");

    // 左右分割（S）→ 叠加（O）互斥：切换叠加后分割必须自动关闭。
    sendKey(ws, Qt::Key_S);
    CHECK(split->isChecked(), "key S turns split compare ON");
    sendKey(ws, Qt::Key_O);
    CHECK(overlay->isChecked(), "key O turns overlay compare ON");
    CHECK(!split->isChecked(), "overlay auto-unchecks split (no stale mode state)");

    // 棋盘格（K）→ 叠加自动关闭。
    if (checker && checker->isEnabled())
    {
        sendKey(ws, Qt::Key_K);
        CHECK(checker->isChecked(), "key K turns checkerboard compare ON");
        CHECK(!overlay->isChecked(), "checkerboard auto-unchecks overlay");
        sendKey(ws, Qt::Key_K);
        CHECK(!checker->isChecked(), "key K turns checkerboard OFF");
    }
    else
    {
        // overlay 仍开着：关闭，回到网格。
        sendKey(ws, Qt::Key_O);
        CHECK(!overlay->isChecked(), "key O turns overlay OFF again");
    }
    CHECK(!split->isChecked() && !overlay->isChecked() && !blink->isChecked(),
          "all exclusive modes are OFF after the round-trip (grid restored)");

    // Compare canvas and Blink use different rendering surfaces. They must
    // never remain checked together: otherwise Blink only toggles hidden grid
    // panes while the visible canvas stays static. Cover both user directions
    // and the page/engine state, not only the toolbar checkboxes.
    QWidget *gridPage = ws->findChild<QWidget *>(QStringLiteral("compareGridPage"));
    QWidget *canvasPage = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    CHECK(gridPage && canvasPage && gridPage->isVisible() && !canvasPage->isVisible(),
          "compare starts on the normal grid page when no canvas mode is active");

    auto findComparePane = [ws](int index) -> RawImageView *
    {
        for (RawImageView *view : ws->findChildren<RawImageView *>())
            if (view && view->cellIndex() == index)
                return view;
        return nullptr;
    };
    auto paneGeometryValid = [](RawImageView *view)
    {
        return view && view->isVisible() && view->width() > 0 && view->height() > 0 &&
               view->sourceSize().isValid();
    };
    auto paneFitValid = [&paneGeometryValid](RawImageView *view)
    {
        if (!paneGeometryValid(view))
            return false;
        const QSize source = view->sourceSize();
        const double fit = std::min(static_cast<double>(view->width()) / source.width(),
                                    static_cast<double>(view->height()) / source.height());
        return fit > 0.0 && qIsFinite(view->scale()) && view->scale() > 0.0 &&
               qAbs(view->scale() / fit - 1.0) < 0.05;
    };
    auto waitForGridPaneFit = [&](bool &geometry, bool &fit)
    {
        geometry = false;
        fit = false;
        QElapsedTimer settled;
        settled.start();
        while ((!geometry || !fit) && settled.elapsed() < 1500)
        {
            pump(25);
            RawImageView *pane0 = findComparePane(0);
            RawImageView *pane1 = findComparePane(1);
            geometry = paneGeometryValid(pane0) && paneGeometryValid(pane1);
            fit = paneFitValid(pane0) && paneFitValid(pane1);
        }
    };
    sendKey(ws, Qt::Key_O);
    pump(30);
    CHECK(overlay->isChecked() && !blink->isChecked() && canvasPage && canvasPage->isVisible() &&
              gridPage && !gridPage->isVisible(),
          "overlay activates the canvas page without enabling Blink");
    sendKey(ws, Qt::Key_B);
    pump(30);
    CHECK(blink->isChecked() && !overlay->isChecked() && gridPage && gridPage->isVisible() &&
              canvasPage && !canvasPage->isVisible(),
          "Blink cancels Overlay and returns to the grid page");
    CHECK(ws->engine().blinkController().isBlinking(),
          "Blink starts the engine blink state after leaving the canvas");
    sendKey(ws, Qt::Key_B);
    pump(40);
    CHECK(!blink->isChecked() && !ws->engine().blinkController().isBlinking() && gridPage &&
              gridPage->isVisible(),
          "stopping Blink clears the engine state and restores the grid");
    bool blinkStopGeometry = false;
    bool blinkStopFit = false;
    waitForGridPaneFit(blinkStopGeometry, blinkStopFit);
    CHECK(blinkStopGeometry,
          "O to B to stopping Blink restores visible geometry for both panes");
    CHECK(blinkStopFit,
          "O to B to stopping Blink restores a valid Fit ratio for both panes");

    sendKey(ws, Qt::Key_B);
    pump(30);
    CHECK(blink->isChecked() && ws->engine().blinkController().isBlinking(),
          "Blink can be started again from the normal grid");
    sendKey(ws, Qt::Key_O);
    pump(60);
    CHECK(overlay->isChecked() && !blink->isChecked() &&
              !ws->engine().blinkController().isBlinking() && canvasPage &&
              canvasPage->isVisible() && gridPage && !gridPage->isVisible(),
          "opening Overlay stops Blink and switches to the canvas page");
    sendKey(ws, Qt::Key_O);
    pump(40);
    CHECK(!overlay->isChecked() && gridPage && gridPage->isVisible() && canvasPage &&
              !canvasPage->isVisible(),
          "closing Overlay returns to the normal grid page");

    bool gridPanesHaveGeometry = false;
    bool gridPanesHaveFit = false;
    waitForGridPaneFit(gridPanesHaveGeometry, gridPanesHaveFit);
    CHECK(gridPanesHaveGeometry,
          "Canvas to Grid restores visible geometry for both compare panes");
    CHECK(gridPanesHaveFit,
          "Canvas to Grid restores a valid Fit display ratio for both compare panes");

    // Compare resize must follow the same relative-zoom contract as the
    // post-layout transitions: Fit follows the new pane geometry, while an
    // intentional wheel zoom keeps its ratio relative to each pane's Fit.
    auto paneFitRatio = [](RawImageView *view)
    {
        if (!view || view->width() <= 0 || view->height() <= 0 || !view->sourceSize().isValid())
            return 0.0;
        const QSize source = view->sourceSize();
        const double fit = std::min(static_cast<double>(view->width()) / source.width(),
                                    static_cast<double>(view->height()) / source.height());
        return fit > 0.0 && qIsFinite(view->scale()) ? view->scale() / fit : 0.0;
    };
    auto waitForPaneGeometry = [&]()
    {
        QElapsedTimer settled;
        settled.start();
        while (settled.elapsed() < 1500)
        {
            pump(25);
            RawImageView *pane0 = findComparePane(0);
            RawImageView *pane1 = findComparePane(1);
            if (paneGeometryValid(pane0) && paneGeometryValid(pane1))
                return true;
        }
        return false;
    };
    RawImageView *resizePane0 = findComparePane(0);
    RawImageView *resizePane1 = findComparePane(1);
    CHECK(resizePane0 && resizePane1,
          "resize workflow: normal grid exposes both panes before geometry checks");
    if (resizePane0 && resizePane1)
    {
        const QSize originalDialogSize = dlg.size();
        const QSize smallerDialogSize(std::max(640, originalDialogSize.width() - 160),
                                      std::max(480, originalDialogSize.height() - 100));
        CHECK(qAbs(paneFitRatio(resizePane0) - 1.0) < 0.05 &&
                  qAbs(paneFitRatio(resizePane1) - 1.0) < 0.05,
              "resize workflow: Fit starts at a relative zoom ratio of one");

        dlg.resize(smallerDialogSize);
        CHECK(waitForPaneGeometry(),
              "resize workflow: Fit resize settles both pane geometries");
        CHECK(qAbs(paneFitRatio(resizePane0) - 1.0) < 0.05 &&
                  qAbs(paneFitRatio(resizePane1) - 1.0) < 0.05,
              "resize workflow: Fit follows the new pane geometry without zoom drift");

        const QPoint zoomCursor(resizePane0->width() / 2, resizePane0->height() / 2);
        QWheelEvent zoomEvent(QPointF(zoomCursor), resizePane0->mapToGlobal(zoomCursor),
                              QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
        QApplication::sendEvent(resizePane0, &zoomEvent);
        pump(50);
        const double zoomRatioBeforeResize = paneFitRatio(resizePane0);
        const double zoomRatioBeforeResizePeer = paneFitRatio(resizePane1);
        CHECK(zoomRatioBeforeResize > 1.05 &&
                  qAbs(zoomRatioBeforeResize - zoomRatioBeforeResizePeer) < 0.05,
              "resize workflow: real wheel zoom establishes one shared relative ratio");

        dlg.resize(originalDialogSize);
        CHECK(waitForPaneGeometry(),
              "resize workflow: zoomed resize settles both pane geometries");
        CHECK(qAbs(paneFitRatio(resizePane0) - zoomRatioBeforeResize) < 0.05 &&
                  qAbs(paneFitRatio(resizePane1) - zoomRatioBeforeResize) < 0.05,
              "resize workflow: zoomed resize preserves the shared Fit-relative ratio");
    }

    // Diff visualization and highlight are separate preferences, but turning
    // highlight on must make the result visible immediately. Exercise both the
    // keyboard path and the real checkbox path without relying on pixel colors.
    QCheckBox *diffHl = findChk(ws, QStringLiteral("高亮差异"));
    QCheckBox *diffOverlay = ws->findChild<QCheckBox *>(QStringLiteral("diffOverlayToggle"));
    CHECK(diffHl != nullptr, "diff-highlight control is discoverable for the H shortcut");
    CHECK(diffOverlay != nullptr && !diffOverlay->isChecked() && diffHl && !diffHl->isChecked(),
          "diff display and highlight start OFF independently");
    if (diffHl && diffOverlay)
    {
        auto waitForDiffOverlay = [&ws](double &opacity, double minOpacity, double maxOpacity)
        {
            opacity = 0.0;
            QElapsedTimer delivery;
            delivery.start();
            while (delivery.elapsed() < 3000)
            {
                pump(25);
                for (RawImageView *view : ws->findChildren<RawImageView *>())
                {
                    if (view && !view->overlay().isNull() && view->overlayOpacity() >= minOpacity &&
                        view->overlayOpacity() <= maxOpacity)
                    {
                        opacity = view->overlayOpacity();
                        return true;
                    }
                }
            }
            return false;
        };

        sendKey(ws, Qt::Key_H);
        CHECK(diffHl->isChecked() && diffOverlay->isChecked(),
              "key H enables diff display before turning highlight ON");
        double highlightedOpacity = 0.0;
        CHECK(waitForDiffOverlay(highlightedOpacity, 0.7, 1.0),
              "highlighted diff delivers a visible red-style overlay");

        sendKey(ws, Qt::Key_H);
        CHECK(!diffHl->isChecked() && diffOverlay->isChecked(),
              "key H turns highlight OFF without hiding diff display");
        double normalOpacity = 0.0;
        CHECK(waitForDiffOverlay(normalOpacity, 0.1, 0.7) && normalOpacity < highlightedOpacity,
              "turning highlight OFF keeps the ordinary heatmap overlay visible");

        diffHl->click();
        CHECK(diffHl->isChecked() && diffOverlay->isChecked(),
              "clicking highlight keeps diff display enabled");
        double clickedHighlightOpacity = 0.0;
        CHECK(waitForDiffOverlay(clickedHighlightOpacity, 0.7, 1.0),
              "clicking highlight restores a visible highlighted overlay");

        diffOverlay->click();
        pump(40);
        bool allOverlaysCleared = true;
        for (RawImageView *view : ws->findChildren<RawImageView *>())
            if (view && !view->overlay().isNull())
                allOverlaysCleared = false;
        CHECK(!diffOverlay->isChecked() && diffHl->isChecked() && allOverlaysCleared,
              "closing diff display clears overlays but preserves highlight preference");

        diffOverlay->click();
        CHECK(diffOverlay->isChecked() && diffHl->isChecked(),
              "reopening diff display preserves highlighted preference");
        double restoredHighlightOpacity = 0.0;
        CHECK(waitForDiffOverlay(restoredHighlightOpacity, 0.7, 1.0),
              "reopening diff display restores the highlighted overlay");

        // Leave the shared Compare workspace in the ordinary off/off state so
        // the later threshold-gated diff workflow starts from its own contract.
        diffOverlay->click();
        diffHl->click();
    }

    // Space 按住 = momentary A<-B presentation; the continuous Blink state
    // remains independent and therefore must stay unchecked.
    auto *temporaryCompare = ws->findChild<QPushButton *>(QStringLiteral("temporaryCompareButton"));
    RawImageView *temporaryView0 = nullptr;
    for (RawImageView *view : ws->findChildren<RawImageView *>())
        if (view && view->cellIndex() == 0)
            temporaryView0 = view;
    sendKeyPressOnly(ws, Qt::Key_Space);
    CHECK(temporaryCompare && temporaryCompare->isDown() && temporaryView0 &&
              temporaryView0->hasTransientDisplay() && !blink->isChecked(),
          "holding Space starts momentary Compare without starting continuous Blink");
    sendKeyReleaseOnly(ws, Qt::Key_Space);
    CHECK(temporaryView0 && !temporaryView0->hasTransientDisplay() && !temporaryCompare->isDown(),
          "releasing Space stops momentary Compare (no stuck state)");

    // Compare 会话可快照（供退出→重进恢复；round-trip 细节由 compare_session_tests 覆盖）。
    const auto session = ws->compareSession();
    CHECK(session.imageIds.size() == 2, "compareSession snapshots both images before exit");

    RawImageView *referenceView = nullptr;
    for (RawImageView *view : ws->findChildren<RawImageView *>())
    {
        if (view && view->cellIndex() == 1 && view->isVisible() && view->isEnabled())
        {
            referenceView = view;
            break;
        }
    }
    CHECK(referenceView != nullptr, "compare exposes the second pane as a RawImageView");
    if (referenceView)
    {
        auto bundle = ws->buildReportBundle();
        if (bundle.referenceIndex != 1)
        {
            const QPoint point = referenceView->rect().center();
            QMouseEvent event(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(referenceView, &event);
            pump(20);

            QPushButton *lockReference = findBtn(ws, QStringLiteral("锁定基准"));
            CHECK(lockReference != nullptr, "compare exposes the lock-reference button");
            if (lockReference)
                lockReference->click();
            pump(20);
            bundle = ws->buildReportBundle();
        }

        CHECK(bundle.referenceIndex == 1 && bundle.targets.size() == 1,
              "report bundle locks the second pane and emits one target");
        CHECK(bundle.images.size() == 2 && bundle.adjustments.size() == 2,
              "report bundle retains both images and both adjustment states");
        CHECK(bundle.images[static_cast<size_t>(bundle.referenceIndex)] == pathB.toStdString() &&
                  bundle.targets[0].path == pathA.toStdString(),
              "report bundle maps the locked reference to path B and target to path A");
    }

    // ── Cache gate: Compare hover smoothness ──
    // Hover annotations (synced crosshair, ROI, focus border, link markers,
    // size-mismatch badge) must only repaint the live vector layer on top of a
    // viewport-bounded cached base surface — never re-scale the full source
    // image. Each pane reports how many times it actually re-rasterized that
    // surface through the diagnostic QObject property baseSurfaceRenderCount
    // (a dynamic property, not a public API change).
    {
        RawImageView *pane0 = nullptr;
        RawImageView *pane1 = nullptr;
        for (RawImageView *v : ws->findChildren<RawImageView *>())
        {
            if (!v || !v->isVisible() || !v->isEnabled())
                continue;
            if (v->cellIndex() == 0)
                pane0 = v;
            else if (v->cellIndex() == 1)
                pane1 = v;
        }
        CHECK(pane0 != nullptr && pane1 != nullptr,
              "cache gate: normal grid exposes both RawImageView panes");
        if (!pane0 || !pane1)
            return;
        auto renderCount = [](RawImageView *v)
        {
            return v->property("baseSurfaceRenderCount").toULongLong();
        };
        {
            QElapsedTimer t;
            t.start();
            while ((pane0->image().isNull() || pane1->image().isNull()) && t.elapsed() < 5000)
                pump(25);
        }
        pump(50);
        const quint64 first0 = renderCount(pane0);
        const quint64 first1 = renderCount(pane1);
        CHECK(first0 > 0 && first1 > 0,
              "cache gate: each pane rasterized its base surface on first paint");

        // Enable the real synced-crosshair control, then drive real hover moves
        // that are each processed through separate event-loop turns. Every move
        // repaints the live crosshair; none may re-rasterize the source image.
        quint64 before0 = first0;
        quint64 before1 = first1;
        QCheckBox *crosshairChk = findChk(ws, QStringLiteral("同步准星"));
        CHECK(crosshairChk != nullptr, "cache gate: synced-crosshair control is discoverable");
        if (crosshairChk)
        {
            crosshairChk->setChecked(true);
            pump(50);
            before0 = renderCount(pane0);
            before1 = renderCount(pane1);
            const QPoint a(pane0->width() / 2 - 5, pane0->height() / 2 - 5);
            const QPoint b(pane0->width() / 2 + 5, pane0->height() / 2 + 5);
            for (int i = 0; i < 4; ++i)
            {
                // Each synthetic move is processed through its own event-loop
                // turn (pump after every move, not once after all moves) so the
                // repaint it triggers is delivered before the next move — no
                // repaint coalescing can hide a re-rasterization in between.
                sendMouseMove(pane0, a);
                pump(25);
                sendMouseMove(pane0, b);
                pump(25);
            }
            CHECK(pane0->hasCrosshair() && pane1->hasCrosshair(),
                  "cache gate: synced crosshair is live on both panes");
            CHECK(renderCount(pane0) == before0 && renderCount(pane1) == before1,
                  "cache gate: crosshair repaints reuse the cached base surface");
            crosshairChk->setChecked(false);
            pump(30);
        }

        // An authoritative transform change rebuilds every affected pane
        // surface; a repeated identical push invalidates and repaints nothing.
        ws->engine().setScale(2.0);
        ws->engine().setOffset(19.0, 23.0);
        ws->update();
        pump(60);
        const quint64 rebuilt0 = renderCount(pane0);
        const quint64 rebuilt1 = renderCount(pane1);
        CHECK(rebuilt0 > before0 && rebuilt1 > before1,
              "cache gate: authoritative scale/offset change rebuilds the pane surfaces");
        CHECK(qAbs(pane0->scale() - 2.0) < 1e-9 && qAbs(pane1->scale() - 2.0) < 1e-9,
              "cache gate: the authoritative scale reaches both panes");

        ws->update();
        pump(60);
        CHECK(renderCount(pane0) == rebuilt0 && renderCount(pane1) == rebuilt1,
              "cache gate: repeated identical transform push neither invalidates nor repaints");
    }

    // ── Wheel-zoom anchor regression ─────────────────────────────────────────
    // CompareWorkspace anchors wheel zoom in CENTER-RELATIVE coordinates
    // (RawImageView interprets offset as a pan delta from the widget center),
    // so the wheel position must be converted to center-relative before
    // applying the transform. Off-center wheel zoom must keep the same
    // image-space point under the cursor in the synchronized, the independent,
    // and both per-axis mixed modes, and a limit hit must derive the offset
    // from the effective (clamped) factor instead of re-scaling after the
    // offset was already computed.
    {
        RawImageView *pane0 = nullptr;
        RawImageView *pane1 = nullptr;
        for (RawImageView *v : ws->findChildren<RawImageView *>())
        {
            if (!v || !v->isVisible() || !v->isEnabled())
                continue;
            if (v->cellIndex() == 0)
                pane0 = v;
            else if (v->cellIndex() == 1)
                pane1 = v;
        }
        CHECK(pane0 != nullptr && pane1 != nullptr,
              "wheel gate: normal grid exposes both RawImageView panes");
        if (pane0 && pane1)
        {
            // Deterministic 40x transform: the 32x32 source (1280px) overflows
            // every pane dimension, so any cursor position maps inside the image
            // and the anchor math is not perturbed by offset clamping.
            const double kScale = 40.0;
            ws->engine().setScale(kScale);
            ws->engine().setOffset(12.0, -7.0);
            ws->update();
            pump(60);
            const QPoint cursor(pane0->width() * 3 / 4, pane0->height() / 4);
            auto wheelDrift = [pane0, cursor](int dy)
            {
                const QPointF before = pane0->widgetToImage(cursor);
                QWheelEvent ev(QPointF(cursor), pane0->mapToGlobal(cursor), QPoint(0, 0),
                               QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false);
                QApplication::sendEvent(pane0, &ev);
                pump(30);
                const QPointF after = pane0->widgetToImage(cursor);
                return qAbs(after.x() - before.x()) + qAbs(after.y() - before.y());
            };

            // Synchronized path: one wheel must keep the image point under the
            // off-center cursor fixed and push the same transform to both panes.
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: sync wheel zoom keeps the image point under the cursor");
            CHECK(qAbs(pane0->scale() - pane1->scale()) < 1e-9 &&
                      pane0->offset() == pane1->offset(),
                  "wheel gate: synchronized wheel zoom updates all panes uniformly");
            CHECK(ws->engine().syncTransform().scale > 1.0,
                  "wheel gate: engine shared transform records a relative zoom ratio");

            // Independent path: only the hovered pane changes, with the same
            // center-relative anchor semantics.
            ws->setSyncEnabled(false);
            pump(20);
            ws->engine().setCellScale(0, kScale);
            ws->engine().setCellOffset(0, 12.0, -7.0);
            ws->engine().setCellScale(1, kScale);
            ws->engine().setCellOffset(1, -9.0, 5.0);
            ws->update();
            pump(60);
            const double p1Scale = pane1->scale();
            const QPointF p1Off = pane1->offset();
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: independent wheel zoom keeps the image point under the cursor");
            CHECK(pane0->scale() > kScale,
                  "wheel gate: independent wheel zoom grows the hovered pane");
            CHECK(qAbs(pane1->scale() - p1Scale) < 1e-9 && pane1->offset() == p1Off,
                  "wheel gate: independent wheel zoom leaves the other pane untouched");

            // Upper limit: a wheel past 50x must clamp to the ceiling and keep
            // the anchor (offset derived from the effective factor, not a post-zoom
            // clamp that would recompute the offset with the unclamped factor).
            ws->setSyncEnabled(true);
            pump(20);
            ws->engine().setScale(48.0);
            ws->engine().setOffset(12.0, -7.0);
            ws->update();
            pump(60);
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: sync wheel zoom keeps the anchor at the 50x ceiling");
            CHECK(qAbs(pane0->scale() - 50.0) < 1e-9,
                  "wheel gate: sync wheel zoom clamps to the 50x ceiling");
            const QPointF limOff = pane0->offset();
            const quint64 ceilingRenders =
                pane0->property("baseSurfaceRenderCount").toULongLong();
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: further wheel at the ceiling keeps the anchor");
            CHECK(qAbs(pane0->scale() - 50.0) < 1e-9 && pane0->offset() == limOff,
                  "wheel gate: scale stays at 50x with an identical transform");
            CHECK(pane0->property("baseSurfaceRenderCount").toULongLong() == ceilingRenders,
                  "wheel gate: ceiling no-op schedules no pane re-rasterization");

            // Zero wheel delta is ignored: no transform change, no repaint request.
            const double scaleBefore = pane0->scale();
            const QPointF offBefore = pane0->offset();
            const quint64 rendersBefore =
                pane0->property("baseSurfaceRenderCount").toULongLong();
            QWheelEvent zev(QPointF(cursor), pane0->mapToGlobal(cursor), QPoint(0, 0),
                            QPoint(0, 0), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(pane0, &zev);
            pump(30);
            CHECK(pane0->scale() == scaleBefore && pane0->offset() == offBefore,
                  "wheel gate: zero wheel delta produces no transform change");
            CHECK(pane0->property("baseSurfaceRenderCount").toULongLong() == rendersBefore,
                  "wheel gate: zero wheel delta produces no repaint request");

            // Per-axis mixed sync modes drive paintEvent's per-axis selection
            // (scale from the shared transform when zoom sync is on, offset from
            // the shared transform when drag sync is on), so the wheel must honor
            // each toggle independently through the real checkbox controls.
            QCheckBox *zoomSyncChk = findChk(ws, QStringLiteral("同步缩放"));
            QCheckBox *dragSyncChk = findChk(ws, QStringLiteral("同步拖动"));
            CHECK(zoomSyncChk != nullptr && dragSyncChk != nullptr,
                  "wheel gate: both per-axis sync controls must exist for the mixed-mode gate");
            if (zoomSyncChk && dragSyncChk)
            {
                // Zoom sync ON, drag sync OFF: every pane scales together through
                // the shared scale, but each pane keeps its own independently
                // zoomed offset.
                zoomSyncChk->setChecked(true);
                dragSyncChk->setChecked(false);
                pump(30);
                ws->engine().setScale(kScale);
                ws->engine().setOffset(12.0, -7.0);
                ws->engine().setCellOffset(0, 12.0, -7.0);
                ws->engine().setCellOffset(1, -9.0, 5.0);
                ws->update();
                pump(60);
                const double mixAScale = pane0->scale();
                const QPointF mixAOff0 = pane0->offset();
                const QPointF mixAOff1 = pane1->offset();
                CHECK(mixAOff0 != mixAOff1,
                      "wheel gate: mixed-A baseline keeps distinct per-pane offsets");
                CHECK(wheelDrift(120) < 0.2,
                      "wheel gate: mixed-A wheel keeps the image point under the cursor");
                CHECK(pane0->scale() > mixAScale && qAbs(pane0->scale() - pane1->scale()) < 1e-9,
                      "wheel gate: mixed-A wheel scales both panes together");
                CHECK(pane0->offset() != mixAOff0 && pane1->offset() != mixAOff1 &&
                          pane0->offset() != pane1->offset(),
                      "wheel gate: mixed-A wheel zooms each pane's own offset independently");

                // Zoom sync OFF, drag sync ON: only the hovered pane's scale
                // changes while both rendered offsets follow the shared
                // (drag-synced) offset.
                zoomSyncChk->setChecked(false);
                dragSyncChk->setChecked(true);
                pump(30);
                ws->engine().setScale(kScale);
                ws->engine().setOffset(12.0, -7.0);
                ws->engine().setCellScale(0, kScale);
                ws->engine().setCellScale(1, kScale);
                ws->update();
                pump(60);
                const double mixBScale1 = pane1->scale();
                const QPointF mixBOff = pane0->offset();
                CHECK(mixBOff == pane1->offset(),
                      "wheel gate: mixed-B baseline shares one offset across panes");
                CHECK(wheelDrift(120) < 0.2,
                      "wheel gate: mixed-B wheel keeps the image point under the cursor");
                CHECK(pane0->scale() > kScale,
                      "wheel gate: mixed-B wheel grows only the hovered pane");
                CHECK(qAbs(pane1->scale() - mixBScale1) < 1e-9,
                      "wheel gate: mixed-B wheel leaves the other pane's scale");
                CHECK(pane0->offset() == pane1->offset() && pane0->offset() != mixBOff,
                      "wheel gate: mixed-B wheel pans both panes through the shared offset");

                // Restore both sync toggles for the rest of the workflow.
                zoomSyncChk->setChecked(true);
                dragSyncChk->setChecked(true);
                pump(30);
            }
        }
    }

    // Esc 退出 Compare（真实 QDialog::reject 路径）。
    QSlider *thresholdSlider = ws->findChild<QSlider *>("diffThresholdSlider");
    QLabel *thresholdValueLabel = ws->findChild<QLabel *>("diffThresholdValueLabel");
    QSlider *brightnessSlider = ws->findChild<QSlider *>("brightnessSlider");
    QPushButton *resetButton = ws->findChild<QPushButton *>("resetAdjustmentsButton");
    QLabel *metricsLabel = ws->findChild<QLabel *>("diffMetricsLabel");
    QCheckBox *analysisToggle = ws->findChild<QCheckBox *>("analysisPanelToggle");
    QTableWidget *inspector = ws->findChild<QTableWidget *>("pixelInspectorTable");
    QWidget *histogram = ws->findChild<QWidget *>("analysisHistogram");
    CHECK(thresholdSlider && thresholdValueLabel && brightnessSlider && resetButton &&
              metricsLabel && analysisToggle && inspector && histogram,
          "compare analysis controls expose stable object names");
    if (thresholdSlider && thresholdValueLabel && brightnessSlider && resetButton && metricsLabel &&
        analysisToggle && inspector && referenceView)
    {
        analysisToggle->setChecked(true);
        pump(20);
        const QPoint paneCenter = referenceView->rect().center();
        sendMouseMove(referenceView, paneCenter);
        sendLeftClick(referenceView, paneCenter);
        CHECK(inspector->rowCount() == 2,
              "real pane click selects the edit target and populates the inspector");

        // CompareEngine is the viewport SSOT. Establish zoom/pan through the same
        // state path used by real wheel/drag input before testing image replacement.
        ws->engine().setScale(2.0);
        ws->engine().setOffset(19.0, 23.0);
        ws->update();
        pump(20);
        const double savedScale = referenceView->scale();
        const QPointF savedOffset = referenceView->offset();

        // Deterministic inspected coordinate: under the fixed transform (scale
        // 2.0, offset (19,23)) the widget point (width/2-12, height/2-8) maps to
        // image pixel (0,0) for any pane geometry, so the Inspector samples a
        // known pixel of the displayed pane image throughout this section.
        sendMouseMove(referenceView,
                      QPoint(referenceView->width() / 2 - 12, referenceView->height() / 2 - 8));

        // M29: diff overlays + metrics are computed asynchronously as ONE batch
        // per refresh request on the AnalysisPool. Every assertion below waits
        // for the delivery instead of assuming a synchronous refresh.
        waitForAnalysisIdle();
        const QString initialMetrics = metricsLabel->text();
        const QString initialInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();

        thresholdSlider->setValue(200);
        waitForAnalysisIdle();
        const QString thresholdMetrics = waitForMetricsChange(metricsLabel, initialMetrics);
        const QString thresholdInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        const auto thresholdBundle = ws->buildReportBundle();
        CHECK(ws->compareSession().threshold == 200 && thresholdBundle.threshold == 200,
              "threshold slider value reaches the compare report session");
        CHECK(thresholdValueLabel->text() == "200",
              "threshold value label follows the slider immediately");
        CHECK(thresholdMetrics != initialMetrics, "threshold change refreshes the report metrics");

        thresholdSlider->setSliderDown(true);
        thresholdSlider->setValue(0);
        pump(20);
        CHECK(thresholdValueLabel->text() == "0" && metricsLabel->text() == thresholdMetrics,
              "threshold drag defers expensive metrics refresh");
        thresholdSlider->setSliderDown(false);
        waitForMetricsText(metricsLabel, initialMetrics);
        CHECK(metricsLabel->text() == initialMetrics,
              "threshold release refreshes deferred metrics");
        thresholdSlider->setValue(200);
        waitForAnalysisIdle();

        // ── M29 gate: rapid threshold changes schedule ONE Analysis batch per
        // refresh request (never recompute inline on the UI thread), and only
        // the FINAL threshold may land on the overlay (latest-wins). A
        // release-gated blocker occupies the single Analysis worker so the two
        // queued batches are observed deterministically.
        {
            auto &sched = TaskScheduler::instance();
            sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
            struct RestoreAnalysisThreads
            {
                ~RestoreAnalysisThreads()
                {
                    TaskScheduler::instance().setQueueMaxThreads(
                        TaskScheduler::Priority::Analysis,
                        std::max(1, QThread::idealThreadCount() / 2));
                }
            } restoreAnalysis;

            // The reference pane was locked to pane 2 earlier, so pane 0 is the
            // target and pane 1 the base. Confirm the delivered overlay equals
            // the threshold-200 heatmap before gating.
            RawImageView *targetView = nullptr;
            for (RawImageView *v : ws->findChildren<RawImageView *>())
                if (v && v->cellIndex() == 0)
                    targetView = v;
            CHECK(targetView != nullptr, "gate test: target pane (0) is a RawImageView");
            if (targetView)
            {
                QCheckBox *diffOverlay = ws->findChild<QCheckBox *>("diffOverlayToggle");
                CHECK(diffOverlay != nullptr,
                      "gate test: explicit diff visualization control is discoverable");
                CHECK(targetView->overlay().isNull(),
                      "gate test: ordinary Compare presents the source image without an overlay");
                if (diffOverlay)
                    diffOverlay->setChecked(true);
                waitForAnalysisIdle();
                const int targetBase = 1;
                const uint8_t finalThreshold = 180;
                const ImageData basePx = ws->engine().imageAt(targetBase)->pixels();
                const ImageData tgtPx = ws->engine().imageAt(0)->pixels();
                const auto expectedHeat = [&](uint8_t t) {
                    return mvcore::toQImage(DifferenceEngine::heatMap(
                        DifferenceEngine::applyThreshold(
                            DifferenceEngine::differenceMap(tgtPx, basePx), t)));
                };
                const QImage expectedBaseline = expectedHeat(200);
                const QImage expectedFinal = expectedHeat(finalThreshold);
                waitForAnalysisIdle();
                CHECK(targetView->overlay() == expectedBaseline,
                      "gate test: baseline overlay matches the threshold-200 heatmap");

                // Release-gated blocker: predicate wait on a condition variable
                // (no busy 1ms sleep). The RAII guard releases on every path so
                // the single Analysis worker can never stay blocked.
                std::mutex gateMtx;
                std::condition_variable gateCv;
                bool gateReleased = false;
                auto blocker = sched.submit(
                    TaskScheduler::Priority::Analysis,
                    [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                    {
                        std::unique_lock<std::mutex> lk(gateMtx);
                        gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                    });
                struct ReleaseGateGuard
                {
                    std::mutex &mtx;
                    std::condition_variable &cv;
                    bool &released;
                    ~ReleaseGateGuard()
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        released = true;
                        cv.notify_all();
                    }
                } releaseGate{gateMtx, gateCv, gateReleased};
                {
                    QElapsedTimer t;
                    t.start();
                    while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
                           t.elapsed() < 5000)
                        pump(10);
                }
                CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
                      "gate test: release-gated blocker occupies the Analysis worker");

                const uint64_t submittedBefore =
                    sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                thresholdSlider->setValue(120);
                thresholdSlider->setValue(finalThreshold);
                pump(20);
                const uint64_t submittedAfter =
                    sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                CHECK(submittedAfter == submittedBefore + 2,
                      "gate test: each threshold change schedules exactly one Analysis "
                      "batch, not inline compute");
                CHECK(targetView->overlay() == expectedBaseline,
                      "gate test: overlay keeps the old state while the batch is queued");

                {
                    std::lock_guard<std::mutex> lk(gateMtx);
                    gateReleased = true;
                }
                gateCv.notify_all();
                CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                                  std::chrono::seconds(15)),
                      "gate test: Analysis pool drains after the gate release");
                {
                    QElapsedTimer t;
                    t.start();
                    while (targetView->overlay() != expectedFinal && t.elapsed() < 5000)
                        pump(25);
                }
                CHECK(targetView->overlay() == expectedFinal,
                      "gate test: final threshold lands on the overlay (latest-wins)");
                CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).pending == 0,
                      "gate test: Analysis scheduler converges after rapid changes");
                CHECK(sched.graphMetrics().handles == 0,
                      "gate test: no live scheduler handles after rapid changes");

                // Restore the 200 threshold baseline for the sections below.
                thresholdSlider->setValue(200);
                waitForAnalysisIdle();
            }
        }

        brightnessSlider->setValue(40);
        waitForAnalysisIdle();
        const QString adjustedMetrics = waitForMetricsChange(metricsLabel, thresholdMetrics);
        const QString adjustedInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        const auto adjustedBundle = ws->buildReportBundle();
        CHECK(brightnessSlider->value() == 40 && adjustedMetrics != thresholdMetrics &&
                  adjustedInspector != thresholdInspector && adjustedInspector != initialInspector,
              "brightness changes the selected pane metrics and Inspector RGB");
        CHECK(adjustedBundle.adjustments.size() == 2 &&
                  adjustedBundle.adjustments[0].brightness == 0 &&
                  adjustedBundle.adjustments[1].brightness == 40,
              "pane click selects the second pane as the edit target");
        CHECK(qAbs(referenceView->scale() - savedScale) < 1e-9 &&
                  referenceView->offset() == savedOffset,
              "brightness adjustment preserves the pane transform");

        // ── Async latest-wins display gate ─────────────────────────────────────
        // Contract: each live brightness adjustment schedules ONE cancellable
        // AnalysisPool display batch; the old pane image stays until delivery,
        // stale generations never land, committed metrics stay deferred while the
        // slider is held down, and the Pixel Inspector refreshes when the newest
        // display lands. Return to identity so the gated drag applies exactly two
        // fresh generations (40 then 80).
        brightnessSlider->setValue(0);
        pump(20);
        waitForMetricsText(metricsLabel, thresholdMetrics);
        const QString deferredMetrics = metricsLabel->text();
        {
            auto &sched = TaskScheduler::instance();
            sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
            struct RestoreAnalysisThreads
            {
                ~RestoreAnalysisThreads()
                {
                    TaskScheduler::instance().setQueueMaxThreads(
                        TaskScheduler::Priority::Analysis,
                        std::max(1, QThread::idealThreadCount() / 2));
                }
            } restoreAnalysis;

            // Expected displays from the same production helpers the pane uses:
            // adjustments stay in analysis space, then the display-only copy is
            // materialized through the frame's color profile.
            const ImageData basePx = ws->engine().imageAt(1)->pixels();
            const auto &baseMeta = ws->engine().imageAt(1)->metadata();
            const QImage expectedBright40 =
                mvcore::toDisplayQImage(adjustBrightness(basePx, 40), baseMeta);
            const QImage expectedBright80 =
                mvcore::toDisplayQImage(adjustBrightness(basePx, 80), baseMeta);

            waitForAnalysisIdle();
            const QImage oldPaneImage = referenceView->image();

            // Release-gated blocker occupies the single Analysis worker so the
            // two display batches are observed queued, never delivered.
            std::mutex gateMtx;
            std::condition_variable gateCv;
            bool gateReleased = false;
            auto blocker = sched.submit(
                TaskScheduler::Priority::Analysis,
                [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                {
                    std::unique_lock<std::mutex> lk(gateMtx);
                    gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                });
            struct ReleaseGateGuard
            {
                std::mutex &mtx;
                std::condition_variable &cv;
                bool &released;
                ~ReleaseGateGuard()
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    released = true;
                    cv.notify_all();
                }
            } releaseGate{gateMtx, gateCv, gateReleased};
            {
                QElapsedTimer t;
                t.start();
                while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
                       t.elapsed() < 5000)
                    pump(10);
            }
            CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
                  "display gate: release-gated blocker occupies the Analysis worker");

            const uint64_t submittedBefore =
                sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
            brightnessSlider->setSliderDown(true);
            brightnessSlider->setValue(40);
            brightnessSlider->setValue(80);
            pump(20);
            const uint64_t submittedAfter =
                sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
            CHECK(submittedAfter == submittedBefore + 2,
                  "display gate: each live brightness adjustment schedules exactly one "
                  "Analysis display batch");
            CHECK(referenceView->image() == oldPaneImage,
                  "display gate: old pane image remains until the display batch delivers");
            CHECK(metricsLabel->text() == deferredMetrics,
                  "display gate: committed metrics stay deferred while the slider is held down");

            {
                std::lock_guard<std::mutex> lk(gateMtx);
                gateReleased = true;
            }
            gateCv.notify_all();
            CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                              std::chrono::seconds(15)),
                  "display gate: Analysis pool drains after the gate release");
            {
                QElapsedTimer t;
                t.start();
                while (referenceView->image() != expectedBright80 && t.elapsed() < 5000)
                    pump(25);
            }
            CHECK(referenceView->image() == expectedBright80,
                  "display gate: final brightness-80 display lands (latest-wins)");
            CHECK(referenceView->image() != expectedBright40,
                  "display gate: stale brightness-40 display never lands");
            CHECK(metricsLabel->text() == deferredMetrics,
                  "display gate: metrics stay deferred while the slider is still held down");
            {
                const QString expectedR = QString::number(qRed(expectedBright80.pixel(0, 0)));
                QElapsedTimer t;
                t.start();
                while ((inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString()) !=
                           expectedR &&
                       t.elapsed() < 5000)
                    pump(25);
            }
            CHECK((inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString()) ==
                      QString::number(qRed(expectedBright80.pixel(0, 0))),
                  "display gate: Pixel Inspector refreshes to the new displayed pixel value");
            CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).pending == 0,
                  "display gate: Analysis scheduler converges after rapid adjustments");
            CHECK(sched.graphMetrics().handles == 0,
                  "display gate: no live scheduler handles after rapid adjustments");
        }
        brightnessSlider->setSliderDown(false);
        waitForMetricsChange(metricsLabel, deferredMetrics);
        CHECK(metricsLabel->text() != deferredMetrics,
              "adjustment release refreshes deferred metrics");

        resetButton->click();
        waitForAnalysisIdle();
        const QString resetInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        waitForMetricsText(metricsLabel, thresholdMetrics);
        CHECK(metricsLabel->text() == thresholdMetrics && resetInspector == thresholdInspector,
              "reset exactly restores the threshold-baseline metrics and Inspector RGB");
        CHECK(qAbs(referenceView->scale() - savedScale) < 1e-9 &&
                  referenceView->offset() == savedOffset,
              "reset preserves the pane transform");
    }

    // ── Pixel Link exact-source / LOD geometry gate ───────────────────────
    // Keep the real CompareWorkspace interaction path, but replace the two
    // pane display rasters with deliberately smaller black LODs.  The engine
    // still owns the original 32x32 source frames, so a tooltip that reads
    // from RawImageView::image() would report black instead of the source RGB.
    // The same setup also exercises the metadata-only placeholder contract by
    // replacing pane 1's analysis pixels with an empty ImageData while its
    // visible LOD remains present.
    {
        RawImageView *linkPane0 = nullptr;
        RawImageView *linkPane1 = nullptr;
        for (RawImageView *view : ws->findChildren<RawImageView *>())
        {
            if (!view || view->cellIndex() < 0 || view->cellIndex() > 1)
                continue;
            if (view->cellIndex() == 0)
                linkPane0 = view;
            else
                linkPane1 = view;
        }
        CHECK(linkPane0 && linkPane1, "pixel-link gate: both real Compare panes are available");
        QCheckBox *pixelLink = findChk(ws, QStringLiteral("像素连线"));
        CHECK(pixelLink != nullptr, "pixel-link gate: real Pixel Link control is discoverable");

        QLabel *linkInfo = nullptr;
        for (QLabel *label : ws->findChildren<QLabel *>())
        {
            if (label && label->text().startsWith(QStringLiteral("标记:")))
            {
                linkInfo = label;
                break;
            }
        }
        CHECK(linkInfo != nullptr, "pixel-link gate: marker summary label is discoverable");
        if (linkPane0 && linkPane1 && pixelLink && linkInfo)
        {
            const QSize sourceSize(32, 32);
            const QRect sourceRect(QPoint(0, 0), sourceSize);
            QImage lod(8, 8, QImage::Format_RGB32);
            lod.fill(Qt::black);
            linkPane0->setImage(lod, sourceSize, sourceRect);
            linkPane1->setImage(lod, sourceSize, sourceRect);
            CHECK(linkPane0->image().width() < linkPane0->sourceSize().width() &&
                      linkPane0->image().height() < linkPane0->sourceSize().height(),
                  "pixel-link gate: display raster is smaller than source geometry");

            const QPoint sourcePoint(20, 24);
            const QPointF mapped = linkPane0->sourcePointToWidget(sourcePoint);
            CHECK(mapped.x() >= 0.0 && mapped.x() <= linkPane0->width() &&
                      mapped.y() >= 0.0 && mapped.y() <= linkPane0->height(),
                  "pixel-link gate: source marker maps inside the pane using sourceSize");
            CHECK(sourcePoint.x() < linkPane0->sourceSize().width() &&
                      sourcePoint.y() < linkPane0->sourceSize().height(),
                  "pixel-link gate: marker coordinate stays within full source bounds");

            pixelLink->setChecked(true);
            sendLeftClick(linkPane0, mapped.toPoint());
            CHECK(linkPane0->linkMarkers().size() == 1 &&
                      qRound(linkPane0->linkMarkers().first().x()) == sourcePoint.x() &&
                      qRound(linkPane0->linkMarkers().first().y()) == sourcePoint.y(),
                  "pixel-link gate: real click stores the source-space marker, not LOD coords");
            const int linkBaseIndex = ws->buildReportBundle().referenceIndex;
            const QString expectedLinkDelta =
                linkBaseIndex == 0 ? QStringLiteral("Δ(-160,0,160)")
                                   : QStringLiteral("Δ(160,0,-160)");
            CHECK(linkInfo->toolTip().contains(QStringLiteral("[0] RGB(200,40,40)")) &&
                      linkInfo->toolTip().contains(QStringLiteral("[1] RGB(40,40,200)")) &&
                      linkInfo->toolTip().contains(expectedLinkDelta),
                  "pixel-link gate: tooltip RGB/Δ comes from exact Compare source pixels");

            std::vector<std::shared_ptr<ImageFrame>> originalFrames;
            for (int c = 0; c < ws->engine().imageCount(); ++c)
                originalFrames.push_back(ws->engine().image(c));
            const std::shared_ptr<ImageFrame> sourceFrame = originalFrames[0];
            const auto placeholderMeta = ws->engine().imageAt(1)->metadata();
            ws->engine().setFrames(
                {sourceFrame, std::make_shared<ImageFrame>(placeholderMeta, ImageData{})});
            pixelLink->setChecked(false);
            pixelLink->setChecked(true);
            CHECK(linkInfo->toolTip().contains(QStringLiteral("[1] 无效")),
                  "pixel-link gate: metadata-only placeholder reports 无效");
            CHECK(!linkInfo->toolTip().contains(QStringLiteral("[1] RGB(0,0,0)")),
                  "pixel-link gate: placeholder never falls back to its display LOD");
            ws->engine().setFrames(originalFrames);
        }
    }

    // Replacing the pair with two missing files must settle into a clear empty
    // state. Controls from the previous successful pair must not remain
    // clickable while the async batch is loading or after both requests fail.
    const QString missingStem =
        QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer-workflow-missing-%1")
                                            .arg(QString::number(
                                                reinterpret_cast<quintptr>(ws), 16)));
    ws->setImages({missingStem + QStringLiteral("-a.png"),
                   missingStem + QStringLiteral("-b.png")});
    CHECK(!blink->isEnabled() && analyzeButton && !analyzeButton->isEnabled() &&
              exportReportButton && !exportReportButton->isEnabled(),
          "Compare loading state disables image-dependent actions immediately");
    QElapsedTimer failedLoadTimer;
    failedLoadTimer.start();
    while (ws->comparedImageCount() != 0 && failedLoadTimer.elapsed() < 5000)
        pump(25);
    CHECK(ws->comparedImageCount() == 0,
          "failed Compare load settles with no usable panes");
    CHECK(!blink->isEnabled() && analyzeButton && !analyzeButton->isEnabled() &&
              exportReportButton && !exportReportButton->isEnabled(),
          "failed Compare load disables image-dependent actions");
    QWidget *failedLoadPage =
        ws->findChild<QWidget *>(QStringLiteral("compareLoadingPage"));
    QLabel *failedLoadLabel =
        ws->findChild<QLabel *>(QStringLiteral("compareLoadingLabel"));
    QProgressBar *failedLoadProgress =
        ws->findChild<QProgressBar *>(QStringLiteral("compareLoadingProgress"));
    CHECK(failedLoadPage && failedLoadPage->isVisible() && failedLoadLabel &&
              (failedLoadLabel->text().contains(QStringLiteral("没有可用")) ||
               failedLoadLabel->text().contains(QStringLiteral("无法加载"))),
          "failed Compare load shows an actionable empty-state explanation");
    CHECK(failedLoadProgress && !failedLoadProgress->isVisible(),
          "failed Compare load hides the indeterminate progress indicator");

    sendKey(ws, Qt::Key_Escape);
    CHECK(!dlg.isVisible(), "Esc closes the Compare dialog");

    // 退出后继续浏览：SSOT 仍然可用且信号正常。
    sel.setCurrentImage(pathA);
    CHECK(sel.currentImage() == pathA, "browsing continues after leaving Compare");
}

void workflow11_compare_fullscreen(const QString &pathA, const QString &pathB)
{
    std::cout << "── Workflow 11: real MainWindow Compare fullscreen contract ──\n";
    MainWindow w;
    w.resize(1000, 700);
    w.show();
    pump(50);
    auto *gallery = w.findChild<ThumbnailPanel *>();
    CHECK(gallery != nullptr, "fullscreen workflow: MainWindow exposes ThumbnailPanel");
    if (!gallery)
        return;
    gallery->setDirectory(QFileInfo(pathA).absolutePath());
    pump(250);
    gallery->selectPaths(QStringList{pathA, pathB}, pathA);
    pump(20);
    SelectionModel *sel = w.findChild<SelectionModel *>();
    CHECK(sel != nullptr, "fullscreen workflow: MainWindow exposes SelectionModel");
    if (!sel)
        return;
    const QStringList expectedSelection{pathA, pathB};
    CHECK(sel->selection() == expectedSelection,
          "fullscreen workflow: two-image selection remains authoritative");
    QAction *compareAction = nullptr;
    for (QAction *action : w.findChildren<QAction *>())
        if (action && action->text().startsWith(QStringLiteral("比较模式")))
            compareAction = action;
    CHECK(compareAction != nullptr, "fullscreen workflow: Compare action is discoverable");
    if (!compareAction)
        return;
    compareAction->trigger();
    pump(300);
    QDialog *compareDialog = w.findChild<QDialog *>("compareDialog");
    CHECK(compareDialog != nullptr && compareDialog->isVisible(),
          "fullscreen workflow: visible Compare dialog is discoverable");
    CHECK(compareDialog && compareDialog->property("mviewerFullscreenRequested").toBool(),
          "fullscreen workflow: real Compare requests fullscreen before its first frame");
    if (QGuiApplication::platformName() != "offscreen")
        CHECK(compareDialog && compareDialog->isFullScreen(),
              "fullscreen workflow: native Compare opens directly fullscreen");
    if (compareDialog)
    {
        const QString browseStatus = w.statusBar()->currentMessage();
        CompareWorkspace *compare = compareDialog->findChild<CompareWorkspace *>();
        QCheckBox *pixelLink = compare ? findChk(compare, QStringLiteral("像素连线")) : nullptr;
        RawImageView *linkPane = nullptr;
        if (compare)
        {
            for (RawImageView *view : compare->findChildren<RawImageView *>())
            {
                // Double-click pane 1 to lock its reference through the real
                // focusRequested path; Pixel Link's single click must not change it.
                if (view && view->cellIndex() == 1 && view->isVisible() && view->isEnabled())
                {
                    linkPane = view;
                    break;
                }
            }
        }
        CHECK(!browseStatus.isEmpty(),
              "fullscreen workflow: Browse has a status message before Compare owns it");
        CHECK(pixelLink && linkPane,
              "fullscreen workflow: real Compare exposes Pixel Link and an interactive pane");
        if (pixelLink && linkPane)
        {
            const QPoint paneCenter = linkPane->rect().center();
            QMouseEvent focusDbl(QEvent::MouseButtonDblClick, QPointF(paneCenter),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(linkPane, &focusDbl);
            pump(50);
            CHECK(sel->selection() == expectedSelection,
                  "fullscreen workflow: pane 1 focus keeps the Browse pair selection");
            CHECK(sel->currentImage() == pathB,
                  "fullscreen workflow: pane 1 double-click focuses B in Browse SSOT");

            pixelLink->setChecked(true);
            sendLeftClick(linkPane, paneCenter);
            CHECK(w.statusBar()->currentMessage().contains(QStringLiteral("像素连线")),
                  "fullscreen workflow: Pixel Link publishes its transient status");
            CHECK(sel->selection() == expectedSelection,
                  "fullscreen workflow: Pixel Link click preserves the Browse pair selection");
            CHECK(sel->currentImage() == pathB,
                  "fullscreen workflow: Pixel Link click preserves Browse current B");
        }
        const QString expectedBrowseStatus =
            QString("当前: %1").arg(QFileInfo(sel->currentImage()).fileName());
        sendKey(compareDialog, Qt::Key_Escape);
        pump(50);
        CHECK(!compareDialog->isVisible(), "fullscreen workflow: Escape closes fullscreen Compare");
        CHECK(!w.statusBar()->currentMessage().contains(QStringLiteral("像素连线")) &&
                  w.statusBar()->currentMessage() == expectedBrowseStatus,
              "fullscreen workflow: leaving Compare restores the current Browse identity");
    }
    w.close();
    pump(20);
}

// ─── Workflow 15: Compare opens at the selected non-first pair ─────────────
void workflow15_compare_nonfirst_pair_navigation(const QString &rootDir)
{
    std::cout << "── Workflow 15: non-first Compare pair navigation ──\n";
    const QString navDirPath =
        QDir(rootDir).filePath(QStringLiteral("compare_nonfirst_pair_navigation"));
    QDir navDir(navDirPath);
    if (navDir.exists())
        navDir.removeRecursively();
    CHECK(QDir().mkpath(navDirPath), "non-first navigation creates an isolated six-image folder");

    const QList<QColor> colors = {QColor(210, 40, 40), QColor(40, 210, 40),
                                  QColor(40, 40, 210),  QColor(210, 210, 40),
                                  QColor(40, 210, 210),  QColor(210, 40, 210)};
    QStringList paths;
    for (int i = 0; i < colors.size(); ++i)
        paths << writePng(navDir, QStringLiteral("nav_%1.png").arg(i, 3, 10, QChar('0')),
                          colors[i]);
    CHECK(paths.size() == 6, "non-first navigation writes six ordered fixtures");

    MainWindow w;
    w.resize(1100, 760);
    w.show();
    pump(75);
    auto *gallery = w.findChild<ThumbnailPanel *>();
    CHECK(gallery != nullptr, "non-first navigation exposes the real gallery");
    if (!gallery)
    {
        w.close();
        pump(20);
        navDir.removeRecursively();
        return;
    }

    gallery->setDirectory(navDirPath);
    QElapsedTimer galleryTimer;
    galleryTimer.start();
    auto galleryHasAllPaths = [&]()
    {
        for (const QString &path : paths)
            if (!gallery->pathList().contains(path))
                return false;
        return true;
    };
    while (!galleryHasAllPaths() && galleryTimer.elapsed() < 10000)
        pump(25);
    CHECK(galleryHasAllPaths(), "non-first navigation discovers all six gallery inputs");

    const QStringList selected{paths[2], paths[3]};
    gallery->selectPaths(selected, paths[2]);
    pump(35);
    SelectionModel *sel = w.findChild<SelectionModel *>();
    CHECK(sel && sel->selection().size() == 2 && sel->selection().contains(paths[2]) &&
              sel->selection().contains(paths[3]),
          "non-first navigation selects the C/D pair in Browse");
    CHECK(sel && (sel->currentImage() == paths[2] || sel->currentImage() == paths[3]),
          "non-first navigation keeps Browse focus inside the selected C/D pair");

    QAction *compareAction = nullptr;
    for (QAction *action : w.findChildren<QAction *>())
    {
        if (action && action->text().startsWith(QStringLiteral("比较模式")))
        {
            compareAction = action;
            break;
        }
    }
    CHECK(compareAction != nullptr, "non-first navigation finds the real Compare action");
    if (!compareAction)
    {
        w.close();
        pump(20);
        navDir.removeRecursively();
        return;
    }
    compareAction->trigger();
    pump(300);

    QDialog *compareDialog = w.findChild<QDialog *>("compareDialog");
    CompareWorkspace *compare = compareDialog ? compareDialog->findChild<CompareWorkspace *>() :
                                                 nullptr;
    CHECK(compareDialog && compareDialog->isVisible() && compare,
          "non-first navigation opens the real Compare workspace");

    auto waitForPair = [&](const QStringList &expected)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000)
        {
            if (compare && compare->comparedImages() == expected)
                return true;
            pump(25);
        }
        return compare && compare->comparedImages() == expected;
    };

    const QStringList firstPair{paths[2], paths[3]};
    const QStringList nextPair{paths[4], paths[5]};
    const QStringList lastPair{paths[0], paths[1]};
    CHECK(waitForPair(firstPair), "non-first navigation opens at the selected C/D pair");
    CHECK(compare && compare->hasPrevPair() && compare->hasNextPair(),
          "non-first navigation exposes both previous and next from C/D");

    auto findNavButton = [](QWidget *root, const QString &marker)
    {
        if (!root)
            return static_cast<QPushButton *>(nullptr);
        for (QPushButton *button : root->findChildren<QPushButton *>())
            if (button->text().contains(marker))
                return button;
        return static_cast<QPushButton *>(nullptr);
    };
    QPushButton *previous = findNavButton(compare, QStringLiteral("上一对"));
    QPushButton *next = findNavButton(compare, QStringLiteral("下一对"));
    QCheckBox *overlay = compare ? findChk(compare, QStringLiteral("叠加对比")) : nullptr;
    CHECK(previous && next && previous->isEnabled() && next->isEnabled(),
          "non-first navigation enables both real pair buttons at C/D");
    CHECK(overlay != nullptr, "non-first navigation exposes the real overlay mode");
    if (overlay)
    {
        overlay->setChecked(true);
        pump(30);
    }

    if (next)
    {
        next->click();
        CHECK(waitForPair(nextPair), "real MainWindow navigation advances C/D to E/F");
        CHECK(compare && compare->hasPrevPair() && !compare->hasNextPair(),
              "real pair buttons report E/F as the final window");
        CHECK(sel && sel->selection() == nextPair,
              "real navigation synchronizes the complete E/F selection to Browse");
        CHECK(sel && sel->currentImage() == nextPair.first(),
              "real navigation focuses E within the E/F selection");
        CHECK(gallery && gallery->selectedPaths() == nextPair,
              "real navigation highlights the complete E/F pair in Browse");
        CHECK(!overlay || overlay->isChecked(),
              "real navigation preserves overlay while advancing");
    }

    if (previous)
    {
        previous->click();
        CHECK(waitForPair(firstPair), "real MainWindow navigation returns E/F to C/D");
        CHECK(sel && sel->selection() == firstPair,
              "real navigation synchronizes the complete C/D selection to Browse");
        CHECK(sel && sel->currentImage() == firstPair.first(),
              "real navigation focuses C within the C/D selection");
        CHECK(gallery && gallery->selectedPaths() == firstPair,
              "real navigation highlights the complete C/D pair in Browse");
        previous->click();
        CHECK(waitForPair(lastPair), "real MainWindow navigation reaches A/B from C/D");
        CHECK(compare && !compare->hasPrevPair() && compare->hasNextPair(),
              "real pair buttons report A/B as the first window");
        CHECK(sel && sel->selection() == lastPair,
              "real navigation synchronizes the complete A/B selection to Browse");
        CHECK(sel && sel->currentImage() == lastPair.first(),
              "real navigation focuses A within the A/B selection");
        CHECK(gallery && gallery->selectedPaths() == lastPair,
              "real navigation highlights the complete A/B pair in Browse");
        CHECK(!overlay || overlay->isChecked(),
              "real navigation preserves overlay while returning");
    }

    if (compareDialog && compareDialog->isVisible())
        sendKey(compareDialog, Qt::Key_Escape);
    pump(50);

    CHECK(sel && sel->selection() == lastPair,
          "leaving Compare keeps the final A/B SelectionModel window");
    CHECK(gallery->selectedPaths() == lastPair,
          "leaving Compare keeps the final A/B Browse selection");

    // Re-enter through the real MainWindow action: the post-navigation Pair
    // must remain the Compare entry selection instead of falling back to the
    // whole directory after SelectionModel was projected to one image.
    compareAction->trigger();
    pump(300);
    compareDialog = w.findChild<QDialog *>(QStringLiteral("compareDialog"));
    compare = compareDialog ? compareDialog->findChild<CompareWorkspace *>() : nullptr;
    CHECK(compareDialog && compareDialog->isVisible() && compare,
          "re-entering Compare after browsing opens the real workspace");
    CHECK(waitForPair(lastPair),
          "re-entering Compare after exit keeps the final A/B pair");
    if (compareDialog && compareDialog->isVisible())
        sendKey(compareDialog, Qt::Key_Escape);
    pump(50);
    w.close();
    pump(20);
    navDir.removeRecursively();
}

// ─── Workflow 13: real Compare report export stays responsive and cancellable ─
// The save dialog is deliberately held open for one watchdog sample.  That
// makes the assertion independent of total action time: a nested Qt dialog
// loop must service the 1ms heartbeat before the full-resolution report build
// is allowed to start on the Background worker.
void workflow13_compare_report_export_nonblocking(const QString &rootDir)
{
    std::cout << "── Workflow 13: Compare report export responsiveness/cancel ──\n";
    const QString reportDirPath =
        QDir(rootDir).filePath(QStringLiteral("compare_report_nonblocking"));
    QDir reportDir(reportDirPath);
    if (reportDir.exists())
        reportDir.removeRecursively();
    CHECK(QDir().mkpath(reportDirPath),
          "report workflow creates its isolated large-image fixture directory");

    constexpr int kLargeEdge = 4096;
    const QString pathA = writePng(reportDir, QStringLiteral("report_a.png"),
                                   QColor(180, 40, 40), kLargeEdge, kLargeEdge);
    const QString pathB = writePng(reportDir, QStringLiteral("report_b.png"),
                                   QColor(40, 40, 180), kLargeEdge, kLargeEdge);
    CHECK(QFileInfo::exists(pathA) && QFileInfo::exists(pathB),
          "report workflow writes two same-size 4096x4096 fixtures");

    const QString outputPath = reportDir.filePath(QStringLiteral("cancelled_report.json"));
    QFile::remove(outputPath);

    auto &sched = TaskScheduler::instance();
    const bool metadataIdle = sched.drain(TaskScheduler::PoolType::MetadataPool,
                                          std::chrono::seconds(15));
    CHECK(metadataIdle, "report workflow starts with an idle Background pool");

    {
        // The real save dialog is nested and leaves Qt's offscreen modal
        // internals attached to this parent. Keep this terminal fixture window
        // alive until process teardown; deleting it here deadlocks Qt 6.10's
        // offscreen plugin after the dialog returns.
        MainWindow *ownedWindow = new MainWindow;
        MainWindow &w = *ownedWindow;
        w.resize(1200, 800);
        w.show();
        pump(75);

        auto *gallery = w.findChild<ThumbnailPanel *>();
        CHECK(gallery != nullptr, "report workflow exposes the real gallery");
        if (!gallery)
        {
            w.close();
            pump(20);
            reportDir.removeRecursively();
            return;
        }

        gallery->setDirectory(reportDirPath);
        QElapsedTimer galleryTimer;
        galleryTimer.start();
        while ((!gallery->pathList().contains(pathA) || !gallery->pathList().contains(pathB)) &&
               galleryTimer.elapsed() < 8000)
            pump(25);
        CHECK(gallery->pathList().contains(pathA) && gallery->pathList().contains(pathB),
              "report workflow discovers both large Compare inputs");
        gallery->selectPaths(QStringList{pathA, pathB}, pathA);
        pump(25);

        QAction *compareAction = nullptr;
        for (QAction *action : w.findChildren<QAction *>())
        {
            if (action && action->text().startsWith(QStringLiteral("比较模式")))
            {
                compareAction = action;
                break;
            }
        }
        CHECK(compareAction != nullptr, "report workflow finds the real Compare action");
        if (!compareAction)
        {
            w.close();
            pump(20);
            reportDir.removeRecursively();
            return;
        }
        compareAction->trigger();

        QDialog *compareDialog = nullptr;
        CompareWorkspace *workspace = nullptr;
        QElapsedTimer compareTimer;
        compareTimer.start();
        while (compareTimer.elapsed() < 12000)
        {
            compareDialog = w.findChild<QDialog *>(QStringLiteral("compareDialog"));
            workspace = compareDialog ? compareDialog->findChild<CompareWorkspace *>() : nullptr;
            if (compareDialog && compareDialog->isVisible() && workspace &&
                workspace->comparedImageCount() == 2)
                break;
            pump(25);
        }
        CHECK(compareDialog && compareDialog->isVisible(),
              "report workflow opens the real Compare dialog");
        CHECK(workspace && workspace->comparedImageCount() == 2,
              "report workflow waits for both full-resolution Compare frames");
        if (!compareDialog || !workspace || workspace->comparedImageCount() != 2)
        {
            if (compareDialog)
                compareDialog->close();
            w.close();
            pump(20);
            reportDir.removeRecursively();
            return;
        }

        // Compare's own AnalysisPool work is not part of this regression.  Let
        // it settle, then drain any MainWindow metadata debounce before the
        // release-gated Background blocker is installed.
        waitForAnalysisIdle(15000);
        const bool idleAfterCompare = sched.drain(TaskScheduler::PoolType::MetadataPool,
                                                  std::chrono::seconds(15));
        CHECK(idleAfterCompare, "report workflow drains pre-existing Background work");

        BackgroundBlocker blocker;
        bool blockerActive = false;
        QElapsedTimer blockerTimer;
        blockerTimer.start();
        while (blockerTimer.elapsed() < 3000)
        {
            pump(5);
            const auto metrics = sched.metrics(TaskScheduler::PoolType::MetadataPool);
            if (blocker.task && blocker.control->entered.load(std::memory_order_acquire) &&
                metrics.active_tasks > 0)
            {
                blockerActive = true;
                break;
            }
        }
        CHECK(blockerActive, "report workflow confirms the sole Background worker is occupied");

        QPushButton *reportButton = nullptr;
        for (QPushButton *button : compareDialog->findChildren<QPushButton *>())
        {
            if (button && button->text().startsWith(QStringLiteral("导出报告")))
            {
                reportButton = button;
                break;
            }
        }
        CHECK(reportButton != nullptr && reportButton->isEnabled(),
              "report workflow finds an enabled real Compare export button");
        if (!reportButton)
        {
            blocker.release();
            CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool,
                              std::chrono::seconds(15)),
                  "report workflow drains Background blocker after missing export button");
            compareDialog->close();
            w.close();
            pump(20);
            reportDir.removeRecursively();
            return;
        }

        std::atomic<uint64_t> heartbeatTicks{0};
        std::atomic<bool> actionStarted{false};
        std::atomic<bool> watchdogSawHeartbeat{false};
        QTimer heartbeat;
        heartbeat.setInterval(1);
        QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat,
                         [&heartbeatTicks]()
                         { heartbeatTicks.fetch_add(1, std::memory_order_relaxed); });
        heartbeat.start();

        std::atomic<bool> saveDialogSeen{false};
        bool saveDialogAccepted = false;
        QTimer dialogPoll;
        dialogPoll.setInterval(1);
        QObject::connect(&dialogPoll, &QTimer::timeout, &dialogPoll,
                         [&dialogPoll, &saveDialogSeen, &saveDialogAccepted, outputPath]()
                         {
                             for (QWidget *top : QApplication::topLevelWidgets())
                             {
                                 auto *dialog = qobject_cast<QFileDialog *>(top);
                                 if (!dialog || !dialog->isVisible())
                                     continue;
                                 if (saveDialogSeen.exchange(true, std::memory_order_acq_rel))
                                     return;
                                 dialog->selectFile(outputPath);
                                 dialogPoll.stop();
                                 QPointer<QFileDialog> guard(dialog);
                                 QTimer::singleShot(
                                     300, dialog,
                                     [guard, &saveDialogAccepted]()
                                     {
                                         if (!guard)
                                             return;
                                         saveDialogAccepted = true;
                                         QMetaObject::invokeMethod(guard.data(), "accept",
                                                                   Qt::DirectConnection);
                                     });
                                 return;
                             }
                         });

        // A missing non-native dialog must fail the assertions without leaving
        // a modal loop hanging forever on a different Qt platform plugin.
        QTimer dialogTimeout;
        dialogTimeout.setSingleShot(true);
        QObject::connect(&dialogTimeout, &QTimer::timeout, &dialogTimeout,
                         []()
                         {
                             for (QWidget *top : QApplication::topLevelWidgets())
                             {
                                 if (auto *dialog = qobject_cast<QFileDialog *>(top))
                                 {
                                     dialog->reject();
                                     return;
                                 }
                             }
                         });

        dialogPoll.start();
        dialogTimeout.start(5000);
        heartbeatTicks.store(0, std::memory_order_relaxed);
        std::thread watchdog(
            [&actionStarted, &heartbeatTicks, &watchdogSawHeartbeat]()
            {
                while (!actionStarted.load(std::memory_order_acquire))
                    std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                watchdogSawHeartbeat.store(heartbeatTicks.load(std::memory_order_acquire) > 0,
                                           std::memory_order_release);
            });
        actionStarted.store(true, std::memory_order_release);
        reportButton->click();
        if (watchdog.joinable())
            watchdog.join();
        heartbeat.stop();
        dialogPoll.stop();
        dialogTimeout.stop();

        CHECK(saveDialogSeen.load(std::memory_order_acquire) && saveDialogAccepted,
              "report workflow accepts the real non-native save dialog");
        CHECK(watchdogSawHeartbeat.load(std::memory_order_acquire),
              "report export services UI heartbeat before full-resolution work");

        QPointer<QProgressDialog> progress = w.findChild<QProgressDialog *>();
        CHECK(progress && progress->isVisible(),
              "report export shows a cancellable progress dialog while Background is blocked");
        const auto queuedReport = sched.metrics(TaskScheduler::PoolType::MetadataPool);
        CHECK(queuedReport.pending >= 2,
              "report export remains queued behind the release-gated Background task");

        QPushButton *cancelButton = nullptr;
        if (progress)
        {
            for (QPushButton *button : progress->findChildren<QPushButton *>())
            {
                if (button && button->text().startsWith(QStringLiteral("取消")))
                {
                    cancelButton = button;
                    break;
                }
            }
        }
        CHECK(cancelButton != nullptr, "report progress exposes the real 取消 button");
        const TaskScheduler::TaskId blockerId = blocker.task ? blocker.task->id : 0;
        auto reportHandle = newestBackgroundTask(blockerId);
        CHECK(reportHandle != nullptr,
              "report workflow identifies the queued MainWindow Background report task");
        if (progress)
        {
            // The offscreen Qt platform deadlocks when the private button
            // connection re-enters close(). Exercise the public canceled state
            // and emit the production canceled signal directly; this invokes
            // MainWindow::cancelReportExport without a platform re-entry.
            progress->cancel();
            const int canceledSignal =
                QProgressDialog::staticMetaObject.indexOfSignal("canceled()");
            if (canceledSignal >= 0)
                QMetaObject::activate(progress.data(), &QProgressDialog::staticMetaObject,
                                       canceledSignal, nullptr);
        }
        if (reportHandle)
            TaskScheduler::cancel(reportHandle);

        CHECK(!progress || !progress->isVisible(), "report progress closes after cancellation");
        CHECK(progress && progress->wasCanceled(),
              "report progress records cancellation from its real cancel state");
        blocker.release();
        const bool drained = sched.drain(TaskScheduler::PoolType::MetadataPool,
                                          std::chrono::seconds(15));
        CHECK(drained, "report cancellation drains the Background pool");

        CHECK(!QFileInfo::exists(outputPath),
              "cancelled Compare report does not publish the requested output file");
        const QStringList remaining =
            reportDir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        QStringList unexpected;
        for (const QString &name : remaining)
        {
            if (name != QFileInfo(pathA).fileName() && name != QFileInfo(pathB).fileName())
                unexpected << name;
        }
        CHECK(unexpected.isEmpty(),
              "cancelled Compare report leaves no report or .mviewer-tmp-* artifact");
        const auto finalMetrics = sched.metrics(TaskScheduler::PoolType::MetadataPool);
        const auto graph = sched.graphMetrics();
        CHECK(finalMetrics.pending == 0 && finalMetrics.active_tasks == 0 &&
                  finalMetrics.queue_depth == 0,
              "report cancellation converges Background pending/active/queue metrics");
        CHECK(graph.handles == 0, "report cancellation releases scheduler graph handles");

        compareDialog->close();
        w.close();
    }

    reportDir.removeRecursively();
}

void workflow12_compare_mixed_fit(const QString &dirPath)
{
    std::cout << "── Workflow 12: mixed-size per-pane Fit contract ──\n";
    QDir dir(dirPath);
    const QString wide = writePng(dir, "m35_wide.png", QColor(180, 80, 30), 800, 100);
    const QString portrait = writePng(dir, "m35_portrait.png", QColor(30, 100, 180), 100, 500);
    QDialog dlg;
    auto *layout = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    layout->addWidget(ws);
    dlg.resize(1000, 700);
    dlg.show();
    ws->setImages({wide, portrait});
    QElapsedTimer timer;
    timer.start();
    while (ws->comparedImageCount() != 2 && timer.elapsed() < 5000)
        pump(25);
    pump(100);

    RawImageView *pane0 = nullptr;
    RawImageView *pane1 = nullptr;
    for (RawImageView *pane : ws->findChildren<RawImageView *>())
        if (pane && pane->isVisible())
            (pane->cellIndex() == 0 ? pane0 : pane1) = pane;
    CHECK(pane0 && pane1, "mixed-fit workflow exposes both panes");
    if (pane0 && pane1)
    {
        const double fit0 = std::min(double(pane0->width()) / 800.0,
                                     double(pane0->height()) / 100.0);
        const double fit1 = std::min(double(pane1->width()) / 100.0,
                                     double(pane1->height()) / 500.0);
        CHECK(qAbs(pane0->scale() - fit0) < 0.02 && qAbs(pane1->scale() - fit1) < 0.02,
              "mixed-fit workflow fits each image to its own final pane geometry");
        CHECK(qAbs(pane0->scale() / fit0 - pane1->scale() / fit1) < 0.02,
              "mixed-fit workflow starts with one shared relative zoom ratio");

        QCheckBox *uniform = findChk(ws, QStringLiteral("统一像素倍率"));
        CHECK(uniform != nullptr, "mixed-fit workflow exposes Uniform Pixel Scale");
        if (uniform)
        {
            uniform->setChecked(true);
            pump(50);
            CHECK(qAbs(pane0->scale() - pane1->scale()) < 1e-9,
                  "Uniform Pixel Scale alone opts into equal absolute scales");
            uniform->setChecked(false);
            pump(50);
            CHECK(qAbs(pane0->scale() / fit0 - pane1->scale() / fit1) < 0.02,
                  "disabling Uniform restores per-pane Fit semantics");
        }
    }
    dlg.close();
    pump(20);
    QFile::remove(wide);
    QFile::remove(portrait);
}

// ─── Workflow 14: Compare startup loading feedback ───────────────────────────
// A real Decode worker gate makes the first setImages() observable before any
// frame can land. The workflow verifies the non-modal loading page,
// supersession text, terminal page restoration, and Escape while loading.
void workflow14_compare_loading_feedback(const QString &rootDir)
{
    std::cout << "── Workflow 14: compare loading feedback ──\n";

    QDir dir(rootDir);
    const QString pathA =
        writePng(dir, QStringLiteral("compare_load_a.png"), QColor(200, 40, 40));
    const QString pathB =
        writePng(dir, QStringLiteral("compare_load_b.png"), QColor(40, 200, 40));
    const QString pathC =
        writePng(dir, QStringLiteral("compare_load_c.png"), QColor(40, 40, 200));
    auto &sched = TaskScheduler::instance();
    // Keep the gate deterministic even if this workflow is run from a harness
    // with a wider DecodePool than the normal offscreen suite. Restore the
    // suite baseline before returning so later workflows see their contract.
    sched.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    struct RestoreDecodeThreads
    {
        ~RestoreDecodeThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
        }
    } restoreDecodeThreads;
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "loading feedback starts with an idle Decode pool");

    DecodeBlocker blocker;
    bool blockerActive = false;
    QElapsedTimer blockerTimer;
    blockerTimer.start();
    while (blockerTimer.elapsed() < 3000)
    {
        if (blocker.control->entered.load(std::memory_order_acquire))
        {
            blockerActive = true;
            break;
        }
        pump(10);
    }
    CHECK(blockerActive, "loading feedback gate occupies the Decode worker");

    // Escape must close the real non-modal host even while the loading page is
    // current; the page cannot be a modal child or consume the key sequence.
    {
        QDialog escDialog;
        auto *layout = new QVBoxLayout(&escDialog);
        auto *workspace = new CompareWorkspace(&escDialog);
        layout->addWidget(workspace);
        escDialog.resize(800, 500);
        escDialog.show();
        pump(20);
        workspace->setImages({pathA, pathB});
        QWidget *loading = workspace->findChild<QWidget *>(QStringLiteral("compareLoadingPage"));
        QLabel *label = workspace->findChild<QLabel *>(QStringLiteral("compareLoadingLabel"));
        QProgressBar *progress =
            workspace->findChild<QProgressBar *>(QStringLiteral("compareLoadingProgress"));
        CHECK(loading && loading->isVisible(),
              "loading feedback appears immediately after non-empty setImages");
        CHECK(label && label->text().contains(QStringLiteral("2")),
              "loading feedback names the requested image count");
        CHECK(progress && progress->minimum() == 0 && progress->maximum() == 0,
              "loading feedback uses an indeterminate progress indicator");
        CHECK(!escDialog.isModal(), "loading feedback remains in the non-modal Compare host");
        sendKey(&escDialog, Qt::Key_Escape);
        CHECK(!escDialog.isVisible(), "Escape closes Compare while its loading page is visible");
    }

    // Superseding A -> B must update the same visible page, and only B may
    // eventually restore the grid after the gate is released.
    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *workspace = new CompareWorkspace(&dialog);
    layout->addWidget(workspace);
    dialog.resize(800, 500);
    dialog.show();
    pump(20);
    workspace->setImages({pathA, pathB});
    auto *loading = workspace->findChild<QWidget *>(QStringLiteral("compareLoadingPage"));
    auto *label = workspace->findChild<QLabel *>(QStringLiteral("compareLoadingLabel"));
    auto *grid = workspace->findChild<QWidget *>(QStringLiteral("compareGridPage"));
    CHECK(loading && loading->isVisible() && label && label->text().contains(QStringLiteral("2")),
          "first Compare batch owns the visible loading page");
    workspace->setImages({pathA, pathB, pathC});
    CHECK(loading && loading->isVisible() && label && label->text().contains(QStringLiteral("3")),
          "superseding Compare batch updates loading text without hiding it");

    blocker.release();
    QElapsedTimer loadTimer;
    loadTimer.start();
    while (workspace->comparedImageCount() != 3 && loadTimer.elapsed() < 10000)
        pump(25);
    CHECK(workspace->comparedImageCount() == 3,
          "current Compare batch completes with all requested panes");
    pump(50);
    CHECK(loading && !loading->isVisible() && grid && grid->isVisible(),
          "completed Compare batch hides loading and restores the grid");
    sendKey(&dialog, Qt::Key_Escape);
    CHECK(!dialog.isVisible(), "Escape remains available after loading completes");

    QFile::remove(pathA);
    QFile::remove(pathB);
    QFile::remove(pathC);
}

// ─── Workflow 10: Compare canvas page visibility + interaction ───────────────
// The planned Compare refactor exposes two stable, discoverable widgets:
//   - "compareCanvas":  the interactive page owning wheel/drag while a canvas
//     compare mode (split / swipe / overlay / checkerboard) is active.
//   - "compareGridPage": the normal grid page, hidden in canvas modes.
// Hidden RawImageViews cannot receive wheel/drag, so in every canvas mode the
// canvas must be the visible+enabled page. Wheel zoom anchors CENTER-RELATIVE
// to the half-pane under the cursor (split/swipe) or the canvas (overlay);
// left-drag pans the shared offset by the mouse delta; switching canvas modes
// preserves that shared zoom/pan on the same canvas. Red-fails on current code
// because "compareCanvas" does not exist yet.
void workflow10_compare_canvas(const QString &pathA, const QString &pathB)
{
    std::cout << "── Workflow 10: compare canvas visibility + interaction ──\n";

    // Same synthetic two-image setup as Workflow 2.
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({pathA, pathB});
    dlg.resize(1100, 750);
    dlg.show();
    {
        QElapsedTimer cmpLoad;
        cmpLoad.start();
        while (ws->comparedImageCount() != 2 && cmpLoad.elapsed() < 5000)
            pump(25);
    }
    CHECK(ws->comparedImageCount() == 2, "canvas workflow: two images loaded into Compare");

    QWidget *canvas = ws->findChild<QWidget *>("compareCanvas");
    QWidget *gridPage = ws->findChild<QWidget *>("compareGridPage");
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *checker = findChk(ws, QStringLiteral("棋盘对比"));
    CHECK(canvas != nullptr, "canvas workflow: compare exposes the compareCanvas widget");
    if (!canvas || !split || !swipe || !overlay || !checker)
        return; // planned canvas contract is absent — the check above is the red gate
    CHECK(gridPage != nullptr, "canvas workflow: compare exposes the compareGridPage widget");
    if (!gridPage)
        return;

    // Default state: the grid page owns the area, the canvas stays out of it.
    CHECK(gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: the normal grid page is the visible page by default");

    // Split ON → the canvas becomes the visible interactive page.
    split->setChecked(true);
    pump(20);
    CHECK(split->isChecked() && canvas->isVisible() && canvas->isEnabled() && gridPage->isHidden(),
          "canvas workflow: split shows the canvas and hides the grid page");
    CHECK(canvas->width() > 0 && canvas->height() > 0,
          "canvas workflow: the canvas occupies a real layout area");

    // M34: deterministic split geometry contract — the halves must cover the
    // whole canvas with no right-edge or seam gap. This is pure geometry (the
    // exact helper drawSplitCompare paints with), so it does not depend on image
    // aspect ratio, Fit letterboxing, or async pane materialization.
    {
        const QRect cv = canvas->rect();
        const auto halves = CompareWorkspace::splitRects(cv);
        const QRect left = halves.first;
        const QRect right = halves.second;
        CHECK(!left.isEmpty() && !right.isEmpty(),
              "canvas workflow: split halves are non-empty for a real canvas");
        CHECK(left.left() == cv.left() && left.top() == cv.top() && left.bottom() == cv.bottom(),
              "canvas workflow: split left half matches the canvas on left/top/bottom");
        CHECK(right.right() == cv.right() && right.top() == cv.top() &&
                  right.bottom() == cv.bottom(),
              "canvas workflow: split right half matches the canvas on right/top/bottom");
        CHECK(left.right() + 1 == right.left(),
              "canvas workflow: split halves are adjacent at the shared boundary");
        CHECK(left.united(right) == cv,
              "canvas workflow: split halves united cover the whole canvas");
        CHECK(qAbs(left.width() - right.width()) <= 1,
              "canvas workflow: split halves differ in width by at most 1");
    }

    // Split OFF → the grid page returns.
    split->setChecked(false);
    pump(20);
    CHECK(!split->isChecked() && gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: leaving split restores the grid page");

    // ── Cursor-anchored wheel zoom on the canvas ──
    // Split draws one half-pane per image; the shared offset is a pan delta
    // from the HALF-PANE center (not the full canvas center), so the wheel
    // anchor must be the cursor position in half-pane center-relative terms.
    split->setChecked(true);
    pump(20);
    const double baseScale = ws->engine().syncTransform().scale;
    const Vec2 baseOffset = ws->engine().syncTransform().offset;
    const int splitMidX = canvas->width() / 2;
    const QPoint cursor(canvas->width() / 6, canvas->height() / 4); // left half-pane
    const double anchorX = cursor.x() - splitMidX / 2.0;
    const double anchorY = cursor.y() - canvas->height() / 2.0;
    QWheelEvent wheel(QPointF(cursor), canvas->mapToGlobal(cursor), QPoint(0, 0), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel);
    pump(20);
    const double zoomedScale = ws->engine().syncTransform().scale;
    const Vec2 zoomedOffset = ws->engine().syncTransform().offset;
    CHECK(zoomedScale > baseScale, "canvas workflow: wheel on the canvas zooms in");
    {
        const double factor = zoomedScale / baseScale;
        const double expectX = anchorX - (anchorX - baseOffset.x) * factor;
        const double expectY = anchorY - (anchorY - baseOffset.y) * factor;
        CHECK(qAbs(zoomedOffset.x - expectX) < 0.5 && qAbs(zoomedOffset.y - expectY) < 0.5,
              "canvas workflow: canvas wheel keeps the image point under the cursor "
              "(center-relative anchor math)");
    }

    // ── Left-drag pans the shared offset by the mouse delta ──
    {
        const Vec2 offBefore = ws->engine().syncTransform().offset;
        const QPoint p0 = canvas->rect().center() - QPoint(40, 30);
        const QPoint p1 = canvas->rect().center() + QPoint(60, 20);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(p0), canvas->mapToGlobal(p0),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(p1), canvas->mapToGlobal(p1), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p1), canvas->mapToGlobal(p1),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &release);
        pump(20);
        const Vec2 offAfter = ws->engine().syncTransform().offset;
        CHECK(qAbs(offAfter.x - (offBefore.x + p1.x() - p0.x())) < 1e-6 &&
                  qAbs(offAfter.y - (offBefore.y + p1.y() - p0.y())) < 1e-6,
              "canvas workflow: left-drag on the canvas pans the shared offset by the mouse "
              "delta");
    }

    // ── Canvas modes preserve the shared zoom/pan on the SAME canvas ──
    {
        const double keptScale = ws->engine().syncTransform().scale;
        const Vec2 keptOffset = ws->engine().syncTransform().offset;

        overlay->setChecked(true);
        pump(20);
        CHECK(overlay->isChecked() && !split->isChecked(),
              "canvas workflow: overlay replaces split (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: overlay keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: overlay preserves the shared zoom/pan from split");

        checker->setChecked(true);
        pump(20);
        CHECK(checker->isChecked() && !overlay->isChecked(),
              "canvas workflow: checkerboard replaces overlay (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: checkerboard keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: checkerboard preserves the shared zoom/pan across modes");

        swipe->setChecked(true);
        pump(20);
        CHECK(swipe->isChecked() && !checker->isChecked(),
              "canvas workflow: swipe replaces checkerboard (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: swipe keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: swipe preserves the shared zoom/pan across modes");

        // The SAME canvas stays the interactive page after the mode switches.
        // Cursor stays far from the swipe divider (default 50% x), so the wheel
        // zooms instead of being treated as divider drag.
        const double swipeScale = ws->engine().syncTransform().scale;
        const QPoint swipeCursor(canvas->width() / 4, canvas->height() * 3 / 4);
        QWheelEvent swipeWheel(QPointF(swipeCursor), canvas->mapToGlobal(swipeCursor), QPoint(0, 0),
                               QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false);
        QApplication::sendEvent(canvas, &swipeWheel);
        pump(20);
        CHECK(ws->engine().syncTransform().scale > swipeScale,
              "canvas workflow: wheel still zooms on the same canvas in swipe mode");
    }

    // Exit every canvas mode → the grid page returns.
    swipe->setChecked(false);
    pump(20);
    CHECK(!split->isChecked() && !overlay->isChecked() && !checker->isChecked() &&
              !swipe->isChecked(),
          "canvas workflow: all canvas compare modes are off");
    CHECK(gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: leaving canvas modes restores the grid page");
}

// ─── Workflow 4: List 首屏只调度可见窗口，不能把全目录送入解码 ────────────────
void workflow4_list_scaling(const QString &rootDir)
{
    std::cout << "── Workflow 4: list scaling ──\n";
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("list_scaling"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    bool fixturesReady = true;
    for (int i = 0; i < 600; ++i)
    {
        QFile file(dir.filePath(QStringLiteral("empty_%1.png").arg(i, 3, 10, QChar('0'))));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            fixturesReady = false;
            break;
        }
    }
    CHECK(fixturesReady, "List scaling creates 600 empty PNG fixtures");

    auto &pipeline = ThumbnailPipeline::instance();
    pipeline.clear();
    const auto totalDecodeStarts = std::make_shared<std::atomic<int>>(0);
    const auto decodePaths = std::make_shared<std::unordered_set<std::string>>();
    const auto decodePathsMutex = std::make_shared<std::mutex>();
    {
        ThumbnailPanel panel;
        panel.setViewMode(ThumbnailPanel::List);
        panel.resize(1200, 800);
        panel.show();
        pump(30);
        pipeline.setDecodeFn(
            [totalDecodeStarts, decodePaths, decodePathsMutex](const std::string &path, int)
            {
                totalDecodeStarts->fetch_add(1, std::memory_order_relaxed);
                const std::lock_guard<std::mutex> lock(*decodePathsMutex);
                decodePaths->insert(path);
                return ImageData{};
            });
        panel.setDirectory(dirPath);
        QElapsedTimer entriesTimer;
        entriesTimer.start();
        while (panel.entries().size() < 600 && entriesTimer.elapsed() < 5000)
            pump(25);
        pump(500);
        size_t uniqueDecodePaths = 0;
        {
            const std::lock_guard<std::mutex> lock(*decodePathsMutex);
            uniqueDecodePaths = decodePaths->size();
        }
        std::cout << "    List decode starts: "
                  << totalDecodeStarts->load(std::memory_order_relaxed) << " total, "
                  << uniqueDecodePaths << " unique paths\n";
        CHECK(panel.entries().size() == 600, "List scaling loads all 600 model entries");
        CHECK(uniqueDecodePaths < 400,
              "List first screen schedules a bounded window instead of all 600 entries");
        pipeline.clear();
    }
    pipeline.clear();
    pipeline.setDecodeFn([](const std::string &path, int size)
                         { return Decoder::decodeScaled(path, size); });
    pump(100);
    dir.removeRecursively();
}

// ─── Workflow 5: Export must use the directory currently shown in the dialog ───
void workflow5_export_current_output_directory(const QString &rootDir)
{
    std::cout << "── Workflow 5: export current output directory ──\n";
    const QString sourceDirPath = QDir(rootDir).filePath(QStringLiteral("export_source"));
    const QString outputDirPath = QDir(rootDir).filePath(QStringLiteral("export_output"));
    QDir().mkpath(sourceDirPath);
    QDir().mkpath(outputDirPath);

    QDir sourceDir(sourceDirPath);
    QDir outputDir(outputDirPath);
    const QString sourcePath =
        writePng(sourceDir, QStringLiteral("export_source.png"), QColor(80, 120, 180));
    const QString sourceReport = sourceDir.filePath(QStringLiteral("export_report.csv"));
    const QString outputReport = outputDir.filePath(QStringLiteral("export_report.csv"));
    QFile::remove(sourceReport);
    QFile::remove(outputReport);

    {
        ExportDialog dialog(QStringList{sourcePath});
        auto *dirEdit = dialog.findChild<QLineEdit *>(QStringLiteral("exportOutputDirectoryEdit"));
        auto *modeCombo = dialog.findChild<QComboBox *>(QStringLiteral("exportModeCombo"));
        CHECK(dirEdit && modeCombo, "export exposes stable output directory and mode controls");
        if (dirEdit && modeCombo)
        {
            dirEdit->setText(outputDir.absolutePath());
            const int csvIndex = modeCombo->findData(QStringLiteral("csv"));
            CHECK(csvIndex >= 0, "export offers CSV mode");
            if (csvIndex >= 0)
            {
                modeCombo->setCurrentIndex(csvIndex);
                CHECK(modeCombo->currentData().toString() == QStringLiteral("csv"),
                      "export CSV mode is selected");
                CHECK(QMetaObject::invokeMethod(&dialog, "onExportClicked", Qt::DirectConnection),
                      "export dispatches asynchronously");
                QElapsedTimer exportTimer;
                exportTimer.start();
                while (!QFileInfo::exists(outputReport) && exportTimer.elapsed() < 5000)
                    pump(25);
                const bool outputExists = QFileInfo::exists(outputReport);
                const bool sourceExists = QFileInfo::exists(sourceReport);
                CHECK(outputExists,
                      std::string("CSV report is written to the edited output directory: ") +
                          outputReport.toStdString());
                CHECK(!sourceExists,
                      std::string("CSV report is not written to the stale source directory: ") +
                          sourceReport.toStdString());
            }
        }
    }

    QFile::remove(sourceReport);
    QFile::remove(outputReport);
    sourceDir.removeRecursively();
    outputDir.removeRecursively();
}

// ─── Workflow 6: MetadataIndexer 双消费者（搜索重建 + Camera filter）────────
// 打开大目录后 MainWindow 在 500ms 后触发搜索重建（reindexSearch），同时
// ThumbnailPanel 的 Camera filter 也触发自己的元数据索引。两个真实消费者
// 共用 MetadataIndexer：任何一方的新请求都不得静默取消另一方。
// 回归点：面板索引在途时被 MainWindow 重建取消 → m_metaIndexing 永远 true →
// Camera filter 永不生效（永久 loading）。
void workflow6_metadata_dual_consumer(const QString &rootDir)
{
    std::cout << "── Workflow 6: metadata dual-consumer (search + camera filter) ──\n";
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("dual_meta"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    // 4000 fake DNGs: 2000 SONY + 2000 NIKON. Enough that the panel's metadata
    // index is still in flight when MainWindow's 500ms re-index fires.
    bool fixturesReady = true;
    for (int i = 0; i < 4000; ++i)
    {
        const std::string p =
            dir.filePath(QStringLiteral("d_%1.dng").arg(i, 4, 10, QChar('0'))).toStdString();
        if (!writeFakeDng(p, (i % 2 == 0) ? "SONY" : "NIKON",
                          QString("BODY%1").arg(i % 5).toStdString(),
                          QString("LENS%1").arg(i % 7).toStdString(),
                          static_cast<uint16_t>(100 * (1 + i % 20))))
        {
            fixturesReady = false;
            break;
        }
    }
    CHECK(fixturesReady, "dual-consumer fixture writes 4000 fake DNGs");

    // Clean session: no recovery prompt, no leftover session state.
    QFile::remove(appConfigFile(QStringLiteral("recovery.json")));

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    pump(100);
    auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
    auto *directoryTree = w.findChild<DirectoryTree *>();
    CHECK(thumbnailPanel != nullptr, "MainWindow exposes the gallery panel");
    CHECK(directoryTree != nullptr, "MainWindow exposes the directory tree");
    if (!thumbnailPanel || !directoryTree)
    {
        dir.removeRecursively();
        return;
    }

    // Real SSOT path: the tree broadcasts directoryChanged -> panel scan +
    // MainWindow re-index scheduling (500ms debounce).
    directoryTree->navigateTo(dirPath, true);
    QElapsedTimer entriesTimer;
    entriesTimer.start();
    while (thumbnailPanel->entries().size() < 4000 && entriesTimer.elapsed() < 30000)
        pump(25);
    std::cout << "    gallery entries after scan: " << thumbnailPanel->entries().size() << " ("
              << entriesTimer.elapsed() << " ms)\n";
    CHECK(thumbnailPanel->entries().size() == 4000, "gallery shows all 4000 rows");

    // Panel filter first: the panel's metadata index must be in flight before
    // MainWindow's 500ms re-index timer fires (folder-load → statsChanged).
    thumbnailPanel->setCameraFilter(QStringLiteral("sony"));

    // Confirm the panel index has genuinely started (shared cache begins
    // filling) before the MainWindow re-index supersedes it.
    QElapsedTimer startTimer;
    startTimer.start();
    while (mviewer::core::MetadataIndexer::instance().size() == 0 && startTimer.elapsed() < 4000)
        pump(10);

    // Now wait out MainWindow's re-index window, then require the camera filter
    // to have applied (exactly the 2000 SONY rows).
    QElapsedTimer filterTimer;
    filterTimer.start();
    while (thumbnailPanel->pathList().size() != 2000 && filterTimer.elapsed() < 20000)
        pump(25);
    CHECK(thumbnailPanel->pathList().size() == 2000,
          "camera filter 'sony' applies while MainWindow re-index runs concurrently "
          "(panel index was NOT silently cancelled)");
    CHECK(mviewer::core::MetadataIndexer::instance().size() >= 2000,
          "shared metadata cache holds the indexed entries");

    w.close();
    pump(100);
    dir.removeRecursively();
}

// ─── Workflow 7: rapid navigation cancels stale preloads, promotes the ──────
// matching neighbor preload to a foreground decode
// Regression for the M29 cancellable viewer loads. Uses the REAL ImageViewer +
// TaskScheduler metrics, deterministic (no wall-clock-only waits): a
// release-gated blocker occupies the single Background (MetadataPool) worker;
// opening the middle image queues its two neighbor preloads behind the blocker.
// Navigating to the LAST image consumes the queued p2 neighbor preload and
// promotes it to the foreground decode — exactly one new DecodePool submission
// (a single foreground escalation, not a re-queue) — while soft-cancelling the
// stale pre_0 preload so its work never runs.
void workflow7_stale_preload_cancellation(const QString &rootDir)
{
    std::cout << "── Workflow 7: stale preload cancellation + matching-neighbor promotion ──\n";

    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    // Fresh folder: these paths have never been decoded anywhere in the suite.
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_preload"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString p0 = writePng(dir, QStringLiteral("pre_0.png"), QColor(210, 40, 40));
    const QString p1 = writePng(dir, QStringLiteral("pre_1.png"), QColor(40, 210, 40));
    const QString p2 = writePng(dir, QStringLiteral("pre_2.png"), QColor(40, 40, 210));

    // Single Background worker (main() already pins it to 1).
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    pump(100);

    ImageViewer *viewer = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *v = qobject_cast<ImageViewer *>(top))
            viewer = v;
    CHECK(viewer != nullptr, "preload test: real viewer window exists");
    if (!viewer)
    {
        w.close();
        dir.removeRecursively();
        return;
    }

    // Drain first so no prior workflow's or MainWindow-startup Background work
    // is in flight before the gate is closed.
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "preload test: Background pool drained before gating");

    // Occupy the single Background worker with a release-gated blocker.
    std::atomic<bool> gate{false};
    auto blocker = sched.submit(TaskScheduler::Priority::Background,
                                [&gate](const TaskScheduler::TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        QElapsedTimer t;
        t.start();
        while (sched.metrics(TaskScheduler::PoolType::MetadataPool).active_tasks < 1 &&
               t.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::MetadataPool).active_tasks >= 1,
          "preload test: release-gated blocker occupies the Background worker");

    // Open the middle image through the real viewer. Its foreground decode runs
    // on DecodePool and delivers on the UI thread; the published Browse
    // sequence drives the two neighbor preloads (pre_0, pre_2) at Background
    // priority -> queued.
    viewer->setBrowseSequence({p0, p1, p2});
    viewer->setImage(p1);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer->frame() || viewer->frame()->metadata().filePath != p1.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer->frame() && viewer->frame()->metadata().filePath == p1.toStdString(),
          "preload test: middle image displayed");

    // The two neighbor preloads must be queued behind the blocker: pending stays
    // >= 3 (blocker + pre_0 + pre_2) and neither has run yet (no cache warm).
    {
        QElapsedTimer t;
        t.start();
        while (sched.metrics(TaskScheduler::PoolType::MetadataPool).pending < 3 &&
               t.elapsed() < 5000)
            pump(10);
    }
    const auto gated = sched.metrics(TaskScheduler::PoolType::MetadataPool);
    CHECK(gated.pending >= 3, "preload test: both neighbor preloads are queued behind the blocker");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage,
                                                  repo.makeKey(p0.toStdString()), probe),
              "preload test: queued neighbor preloads have not run (cache not warmed)");
    }

    // Rapid navigation: open the LAST image while Background is still gated.
    // setImage must consume the queued p2 neighbor preload and promote it to
    // the foreground decode (one DecodePool escalation), while soft-cancelling
    // the stale pre_0 preload. Snapshot DecodePool submissions before the
    // navigation so the promotion costs exactly one submission, measured
    // deterministically (not by wall-clock timing).
    const uint64_t decodeSubmittedBefore =
        sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
    viewer->setImage(p2);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer->frame() || viewer->frame()->metadata().filePath != p2.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer->frame() && viewer->frame()->metadata().filePath == p2.toStdString(),
          "preload test: latest navigation target displayed");

    // p2 was delivered through the normal foreground onLoaded path (the promoted
    // preload is not a special display mode), so its frame must carry a
    // histogram even though p2 arrived as a Background preload first.
    CHECK(viewer->frame() && viewer->frame()->hasHistogram(),
          "preload test: promoted neighbor displays with a histogram (foreground contract)");
    CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted == decodeSubmittedBefore + 1,
          "preload test: matching-neighbor promotion causes exactly one DecodePool submission");

    // Release the gate and drain: pre_0 was ONLY ever a stale queued preload —
    // if rapid navigation did not cancel it, its work would now warm the cache.
    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "preload test: Background drains after the gate release");

    {
        ImageData probe;
        const bool staleCached = CacheManager::instance().getMemory(
            CacheLevel::FullImage, repo.makeKey(p0.toStdString()), probe);
        CHECK(!staleCached,
              "rapid navigation cancels stale neighbor preload work (never warms FullImage)");
    }
    const auto m = sched.metrics(TaskScheduler::PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "preload test: Background scheduler converges after rapid navigation");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "preload test: no live scheduler handles after rapid navigation");

    // Restore the default Background thread count and close.
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background,
                             std::max(1, QThread::idealThreadCount() / 2));
    viewer->close();
    pump(50);
    w.close();
    pump(50);
    dir.removeRecursively();
}

// ─── Workflow 8: PreviewPanel serves a SCALED preview through the ──────────
// Thumbnail pool, never the Decode pool. A real >512 image keeps its source
// dimensions while the held preview pixmap is capped at a 512 max edge, and
// the scaled result lands in the Preview cache without ever warming the
// FullImage cache (regression: the preview used to run a full DecodePool
// loadAsync decode, duplicate ImageViewer decode, and retain a full QPixmap).
void workflow8_preview_scaled_load(const QString &rootDir)
{
    std::cout << "── Workflow 8: preview scaled/cancellable load ──\n";

    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_preview"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString path = dir.filePath(QStringLiteral("preview_big.png"));
    {
        QImage big(1024, 768, QImage::Format_RGB32);
        for (int y = 0; y < 768; y += 8)
        {
            QRgb *row = reinterpret_cast<QRgb *>(big.scanLine(y));
            for (int x = 0; x < 1024; ++x)
                row[x] = qRgb((x * 255) / 1024, (y * 255) / 768, ((x + y) * 255) / 1792);
        }
        CHECK(big.save(path, "PNG"), "preview workflow writes a real 1024x768 PNG");
    }

    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    const std::string key = repo.makeKey(path.toStdString());

    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "preview workflow: Decode pool drained before measuring");
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "preview workflow: Decode pool drained before measuring");
    const uint64_t decodeSubmittedBefore =
        sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, probe),
              "preview workflow: FullImage cache does not hold the key beforehand");
    }

    {
        PreviewPanel panel;
        panel.resize(320, 240);
        QPixmap warm(140, 105);
        warm.fill(QColor(24, 80, 160));
        panel.setImage(path, warm);
        CHECK(panel.hasImage() && panel.presentedPath() == path &&
                  panel.presentationQuality() == PreviewPanel::PresentationQuality::Thumbnail,
              "preview workflow: warm thumbnail is usable synchronously without an event-loop hop");
        QElapsedTimer t;
        t.start();
        while (panel.presentationQuality() != PreviewPanel::PresentationQuality::Preview &&
               t.elapsed() < 10000)
            pump(25);

        CHECK(panel.hasImage() && panel.presentedPath() == path &&
                  panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview,
              "preview workflow: scaled preview atomically upgrades the warm thumbnail");
        CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted ==
                  decodeSubmittedBefore + 1,
              "preview workflow: exactly one foreground Decode task serves the preview");
        {
            ImageData fullProbe;
            CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, fullProbe),
                  "preview workflow: FullImage cache is never touched");
            ImageData previewProbe;
            CHECK(CacheManager::instance().getMemory(
                      CacheLevel::Preview, PreviewPanel::previewCacheKey(path.toStdString()),
                      previewProbe),
                  "preview workflow: scaled result lands in the Preview cache");
        }
        CHECK(panel.sourceImageSize() == QSize(1024, 768),
              "preview workflow: source dimensions are preserved");
        const QSize previewSize = panel.previewPixelSize();
        CHECK(previewSize.width() > 0 && previewSize.height() > 0 &&
                  std::max(previewSize.width(), previewSize.height()) <= 512,
              "preview workflow: preview pixmap is capped at a 512 max edge");

        // Re-selecting the same image reuses the Preview cache: still exactly
        // one foreground Decode submission, dimensions preserved.
        const uint64_t decodeBeforeSecond =
            sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
        panel.setImage(path);
        QElapsedTimer t2;
        t2.start();
        while (!panel.hasImage() && t2.elapsed() < 10000)
            pump(25);
        CHECK(panel.hasImage(), "preview workflow: re-selected preview is delivered");
        CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted ==
                  decodeBeforeSecond + 1,
              "preview workflow: re-select submits exactly one foreground Decode task");
        CHECK(panel.sourceImageSize() == QSize(1024, 768),
              "preview workflow: re-select preserves source dimensions");
    }

    // Scheduler convergence after the second (cache-hit) selection: every
    // preview task — including the delivered one whose handle was released on
    // the UI thread — must drain to zero pending/active/queued work and leave
    // the dependency graph with no live handles before the fixture directory
    // is deleted.
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "preview workflow: Decode pool drains after the cache re-select");
    {
        const TaskScheduler::PoolMetrics pm = sched.metrics(TaskScheduler::PoolType::DecodePool);
        CHECK(pm.pending == 0, "preview workflow: Decode pool has zero pending tasks");
        CHECK(pm.active_tasks == 0, "preview workflow: Decode pool has zero active tasks");
        CHECK(pm.queue_depth == 0, "preview workflow: Decode pool queue is empty");
        CHECK(sched.graphMetrics().handles == 0,
              "preview workflow: scheduler dependency graph has no live handles");
    }

    dir.removeRecursively();
}

// ─── Workflow 9: Pixel Inspector lifecycle ─────────────────────────────────
// Real ImageViewer + real pixelInfo signal over a real decoded ImageFrame.
// QtDecoder normalizes a grayscale PNG to RGB24 (equal channels); moving the
// mouse onto image pixel (3,4) must report valid=true with that exact gray
// value. Every transition away from a live sample — setImage(), Leave, close —
// must synchronously invalidate the inspector (valid=false, x/y=-1): the
// emission count must grow and the last sample must flip to invalid.
void workflow9_pixel_inspector_lifecycle(const QString &rootDir)
{
    std::cout << "── Workflow 9: pixel inspector lifecycle ──\n";

    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_pixel"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString grayPath = dir.filePath(QStringLiteral("gray.png"));
    const QString otherPath = dir.filePath(QStringLiteral("other.png"));
    {
        QImage gray(8, 8, QImage::Format_Grayscale8);
        gray.fill(QColor(73, 73, 73)); // nontrivial gray, never black/white
        CHECK(gray.save(grayPath, "PNG"), "pixel workflow writes a grayscale PNG");
        QImage other(8, 8, QImage::Format_RGB32);
        other.fill(qRgb(200, 100, 50));
        CHECK(other.save(otherPath, "PNG"), "pixel workflow writes a second PNG");
    }

    ImageViewer viewer;
    viewer.resize(320, 240);
    viewer.show();
    pump(50);

    // Capture every pixelInfo emission before the first setImage.
    struct PixelProbe
    {
        int emissions = 0;
        int x = -1;
        int y = -1;
        int r = -1;
        int g = -1;
        int b = -1;
        int a = -1;
        int rawKind = -1;
        bool valid = false;
    };
    PixelProbe probe;
    QObject::connect(
        &viewer, &ImageViewer::pixelInfo, &viewer,
        [&probe](int x, int y, int r, int g, int b, int a, int, int, int, int rawKind, bool valid)
        {
            ++probe.emissions;
            probe.x = x;
            probe.y = y;
            probe.r = r;
            probe.g = g;
            probe.b = b;
            probe.a = a;
            probe.rawKind = rawKind;
            probe.valid = valid;
        });

    viewer.setImage(grayPath);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer.frame() || viewer.frame()->metadata().filePath != grayPath.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    std::shared_ptr<ImageFrame> decodedFrame = viewer.frame();
    CHECK(decodedFrame && decodedFrame->metadata().filePath == grayPath.toStdString(),
          "pixel workflow: grayscale image decodes");
    CHECK(decodedFrame && decodedFrame->width() == 8 && decodedFrame->height() == 8,
          "pixel workflow: decoded frame keeps the 8x8 dimensions");
    // QtDecoder converts the source to RGB888 and stores it as RGB24 ImageData
    // (see toImageData in QtDecoder.cpp), so a grayscale PNG yields equal
    // channels. Sample pixel (3,4) via samplePixel, which canonicalises the
    // format and reports invalid on null/truncated buffers instead of reading
    // out of bounds.
    CHECK(decodedFrame && decodedFrame->pixels().format == PixelFormat::RGB24,
          "pixel workflow: QtDecoder normalizes grayscale PNG to RGB24");
    const PixelRGBA decodedSample =
        decodedFrame ? samplePixel(decodedFrame->pixels(), 3, 4) : PixelRGBA{};
    CHECK(decodedSample.valid, "pixel workflow: decoded sample at (3,4) is readable");
    CHECK(decodedSample.r == decodedSample.g && decodedSample.g == decodedSample.b,
          "pixel workflow: decoded sample channels are equal (gray normalization)");
    const int expectedGray = decodedSample.r;
    CHECK(expectedGray > 0 && expectedGray < 255,
          "pixel workflow: decoded gray value is nontrivial (not black/white)");
    std::cout << "    decoded gray sample: " << expectedGray << "\n";

    // Deterministic transform: zoomActual gives exactly 100% with an integer
    // center offset (156,116) at 320x240. The pixel center (offset+(coord+0.5)
    // *scale) sits on a half-pixel; rounding it up would land on the pixel
    // boundary, so floor keeps the point inside pixel (3,4).
    viewer.zoomActual();
    pump(20);
    CHECK(qAbs(viewer.viewTransform().scale - 1.0) < 1e-9,
          "pixel workflow: zoomActual restores exactly 100%");
    auto screenForPixel = [&viewer](int px, int py)
    {
        const auto v = viewer.viewTransform();
        return QPoint(qFloor(v.offsetX + (px + 0.5) * v.scale),
                      qFloor(v.offsetY + (py + 0.5) * v.scale));
    };
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.emissions >= 1 && probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: mouse over image pixel (3,4) reports a valid sample");
    CHECK(probe.r == expectedGray && probe.g == expectedGray && probe.b == expectedGray,
          "pixel workflow: pixelInfo channels match the decoded gray sample");
    CHECK(probe.a == 255 && probe.rawKind == 0,
          "pixel workflow: RGB24 sample reports opaque alpha and 8-bit kind");

    // Switching images must synchronously invalidate the inspector: the new
    // load emits a cleared sample before the next decode runs, so a stale
    // pixel never lingers while the new image loads.
    const int beforeSwitch = probe.emissions;
    viewer.setImage(otherPath);
    CHECK(probe.emissions > beforeSwitch && !probe.valid,
          "pixel workflow: setImage synchronously invalidates the inspector");
    {
        QElapsedTimer t;
        t.start();
        while (
            (!viewer.frame() || viewer.frame()->metadata().filePath != otherPath.toStdString()) &&
            t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer.frame() && viewer.frame()->metadata().filePath == otherPath.toStdString(),
          "pixel workflow: second image decodes");

    // Re-open the gray image and confirm the inspector recovers.
    viewer.setImage(grayPath);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer.frame() || viewer.frame()->metadata().filePath != grayPath.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer.frame() && viewer.frame()->metadata().filePath == grayPath.toStdString(),
          "pixel workflow: gray image is restored");
    viewer.zoomActual();
    pump(20);
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: inspector recovers after re-opening the image");

    // Cursor leaving the view must invalidate the inspector so the panel stops
    // showing the last hovered pixel.
    const int beforeLeave = probe.emissions;
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(&viewer, &leaveEvent);
    CHECK(probe.emissions > beforeLeave && !probe.valid,
          "pixel workflow: Leave invalidates the inspector");

    // Closing the viewer must invalidate it too: the last sample must not
    // outlive the window.
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: sample is valid again before close");
    const int beforeClose = probe.emissions;
    viewer.close();
    CHECK(probe.emissions > beforeClose && !probe.valid,
          "pixel workflow: close invalidates the inspector");
    pump(50);

    // Drain the pools the real viewer uses (foreground Decode + Background
    // neighbor preloads) before deleting the fixture directory.
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "pixel workflow: Decode pool drains before fixture removal");
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "pixel workflow: Background pool drains before fixture removal");
    dir.removeRecursively();
}

// ── Workflow 16: animated frame stepping keeps render identity and view continuity ───────────
void workflow16_multiframe_view_continuity(const QString &rootDir)
{
    std::cout << "\n=== Workflow 16: multi-frame Viewer continuity ===\n";
    const QString path = writeAnimatedGif(QDir(rootDir), QStringLiteral("wf_animated.gif"));
    CHECK(!path.isEmpty(), "multi-frame workflow writes the deterministic GIF");

    ImageViewer viewer;
    viewer.resize(320, 240);
    viewer.show();
    const ContextMenuSnapshot emptyMenu = contextMenuSnapshot(viewer);
    CHECK(!emptyMenu.enabledLabels.contains(QStringLiteral("复制图片")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("复制路径")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("复制像素颜色 (#RRGGBB)")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("另存为...")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("放大 (+)")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("缩小 (-)")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("适应窗口 (0)")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("实际大小 (1)")) &&
              !emptyMenu.enabledLabels.contains(QStringLiteral("框选区域 (R)")) &&
              emptyMenu.enabledLabels.contains(QStringLiteral("全屏 (F)")),
          "empty viewer disables image-dependent context actions but keeps fullscreen available");
    viewer.setImage(path);
    {
        QElapsedTimer timer;
        timer.start();
        while ((!viewer.frame() || !viewer.isMultiFrame()) && timer.elapsed() < 5000)
            pump(25);
    }
    viewer.pause();
    viewer.setFrameIndex(0);
    {
        QElapsedTimer timer;
        timer.start();
        while (viewer.frameIndex() != 0 && timer.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer.frame() && viewer.frameCount() == 3 && viewer.frameIndex() == 0,
          "multi-frame workflow opens frame 1/3 and can pause playback");
    if (!viewer.frame())
    {
        viewer.close();
        QFile::remove(path);
        return;
    }

    const std::string firstRenderId = viewer.frame()->id().hash;
    const PixelRGBA firstPixel = samplePixel(viewer.frame()->pixels(), 0, 0);
    viewer.zoomActual();
    const Viewport before = viewer.viewTransform();
    sendKey(&viewer, Qt::Key_Period);
    {
        QElapsedTimer timer;
        timer.start();
        while (viewer.frameIndex() != 1 && timer.elapsed() < 5000)
            pump(25);
    }

    const auto secondFrame = viewer.frame();
    const PixelRGBA secondPixel =
        secondFrame ? samplePixel(secondFrame->pixels(), 0, 0) : PixelRGBA{};
    CHECK(firstPixel.valid && firstPixel.r == 255 && firstPixel.g == 0 && firstPixel.b == 0 &&
              secondPixel.valid && secondPixel.r == 0 && secondPixel.g == 255 && secondPixel.b == 0,
          "Period steps from the red frame to the green frame");
    CHECK(secondFrame && secondFrame->id().hash != firstRenderId,
          "each displayed frame has a distinct render-cache identity");
    const Viewport after = viewer.viewTransform();
    CHECK(qAbs(after.scale - before.scale) < 1e-9 &&
              qAbs(after.offsetX - before.offsetX) < 1e-9 &&
              qAbs(after.offsetY - before.offsetY) < 1e-9,
          "frame stepping preserves zoom and pan without re-fitting");
    CHECK(viewer.windowTitle().contains(QStringLiteral("Frame 2/3")) &&
              viewer.windowTitle().contains(QStringLiteral("Paused")),
          "paused frame position remains visible in the Viewer status");
    CHECK(viewer.accessibleDescription().contains(QStringLiteral("Frame 2/3")) &&
              viewer.accessibleDescription().contains(QStringLiteral("Space")),
          "frame status and playback shortcut remain available to assistive UI");
    const ContextMenuSnapshot frameMenu = contextMenuSnapshot(viewer);
    CHECK(frameMenu.labels.contains(QStringLiteral("播放")) &&
              frameMenu.labels.contains(QStringLiteral("重新开始")) &&
              frameMenu.labels.contains(QStringLiteral("上一帧 (,)")) &&
              frameMenu.labels.contains(QStringLiteral("下一帧 (.)")),
          "animated context menu keeps explicit playback and frame navigation");
    CHECK(!frameMenu.enabledLabels.contains(QStringLiteral("上一张 (←)")) &&
              !frameMenu.enabledLabels.contains(QStringLiteral("下一张 (→)")),
          "standalone viewer disables file navigation without a browse sequence");

    viewer.setBrowseSequence({path, path + QStringLiteral(".next"),
                              path + QStringLiteral(".last")});
    const ContextMenuSnapshot firstFileMenu = contextMenuSnapshot(viewer);
    CHECK(!firstFileMenu.enabledLabels.contains(QStringLiteral("上一张 (←)")) &&
              firstFileMenu.enabledLabels.contains(QStringLiteral("下一张 (→)")),
          "browse sequence disables previous navigation at the first image");

    viewer.setBrowseSequence({path + QStringLiteral(".first"), path,
                              path + QStringLiteral(".last")});
    const ContextMenuSnapshot middleFileMenu = contextMenuSnapshot(viewer);
    CHECK(middleFileMenu.enabledLabels.contains(QStringLiteral("上一张 (←)")) &&
              middleFileMenu.enabledLabels.contains(QStringLiteral("下一张 (→)")),
          "browse sequence enables both file directions in the middle");

    viewer.setBrowseSequence({path + QStringLiteral(".first"),
                              path + QStringLiteral(".middle"), path});
    const ContextMenuSnapshot lastFileMenu = contextMenuSnapshot(viewer);
    CHECK(lastFileMenu.enabledLabels.contains(QStringLiteral("上一张 (←)")) &&
              !lastFileMenu.enabledLabels.contains(QStringLiteral("下一张 (→)")),
          "browse sequence disables next navigation at the last image");

    viewer.close();
    QFile::remove(path);

    const QString pagesPath =
        writeMultipageTiff(QDir(rootDir), QStringLiteral("wf_multipage.tiff"));
    CHECK(!pagesPath.isEmpty(), "multi-page workflow writes the deterministic TIFF");
    ImageViewer pages;
    pages.resize(320, 240);
    pages.show();
    pages.setImage(pagesPath);
    {
        QElapsedTimer timer;
        timer.start();
        while ((!pages.frame() || !pages.isMultiFrame()) && timer.elapsed() < 5000)
            pump(25);
    }
    CHECK(pages.frameCount() == 3 && !pages.sequenceInfo().animated,
          "multi-page TIFF opens as a three-page document, not an animation");
    const ContextMenuSnapshot pageMenu = contextMenuSnapshot(pages);
    CHECK(pageMenu.labels.contains(QStringLiteral("第一页")) &&
              pageMenu.labels.contains(QStringLiteral("上一页 (,)")) &&
              pageMenu.labels.contains(QStringLiteral("下一页 (.)")),
          "multi-page context menu exposes document-specific page navigation");
    CHECK(!pageMenu.labels.contains(QStringLiteral("播放")) &&
              !pageMenu.labels.contains(QStringLiteral("暂停")),
          "multi-page context menu does not expose a dead playback action");
    CHECK(!pageMenu.enabledLabels.contains(QStringLiteral("第一页")) &&
              !pageMenu.enabledLabels.contains(QStringLiteral("上一页 (,)")) &&
              pageMenu.enabledLabels.contains(QStringLiteral("下一页 (.)")),
          "multi-page context menu communicates the first-page boundary");

    pages.setFrameIndex(2);
    {
        QElapsedTimer timer;
        timer.start();
        while (pages.frameIndex() != 2 && timer.elapsed() < 5000)
            pump(25);
    }
    const ContextMenuSnapshot lastPageMenu = contextMenuSnapshot(pages);
    CHECK(lastPageMenu.enabledLabels.contains(QStringLiteral("第一页")) &&
              lastPageMenu.enabledLabels.contains(QStringLiteral("上一页 (,)")) &&
              !lastPageMenu.enabledLabels.contains(QStringLiteral("下一页 (.)")),
          "multi-page context menu communicates the last-page boundary");

    pages.close();
    QFile::remove(pagesPath);
}

} // namespace

