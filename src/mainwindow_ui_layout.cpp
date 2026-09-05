// MainWindow layout construction and command surfaces.
#include "mainwindow_p.h"

#include <QSignalBlocker>
#include <QStyle>
#include <QToolBar>

void MainWindow::buildMenus()
{
    auto *menuBar = new QMenuBar(this);
    buildFileMenu(menuBar);
    buildEditMenu(menuBar);
    buildViewMenu(menuBar);
    buildToolsHelpMenus(menuBar);
    setMenuBar(menuBar);
}

void MainWindow::buildFileMenu(QMenuBar *menuBar)
{
    // ----- 文件(&F) -----
    auto *fileMenu = menuBar->addMenu("文件(&F)");
    m_actOpenDir = new QAction("打开目录(&O)...", this);
    m_actOpenDir->setShortcut(QKeySequence::Open); // Ctrl+O
    m_actOpenFile = new QAction("打开文件(&F)...", this);
    m_actOpenFile->setShortcut(QKeySequence("Ctrl+Shift+O"));
    m_actSaveWorkspace = new QAction("保存工作区(&S)", this);
    m_actOpenWorkspace = new QAction("打开工作区(&W)", this);
    m_actSaveProject = new QAction("保存项目(&P)", this);
    m_actOpenProject = new QAction("打开项目(&J)", this);
    m_actExit = new QAction("退出(&Q)", this);
    m_actExit->setShortcut(QKeySequence::Quit); // Ctrl+Q
    fileMenu->addAction(m_actOpenDir);
    fileMenu->addAction(m_actOpenFile);
    fileMenu->addSeparator();

    // P0: Recent folders (from core::RecentFiles LRU) + Favorites (pinned).
    m_recentMenu = fileMenu->addMenu("最近目录(&R)");
    m_recentFileMenu = fileMenu->addMenu("最近文件(&F)");
    m_favMenu = fileMenu->addMenu("收藏目录(&V)");
    m_actAddFavorite = new QAction("收藏当前目录(&D)", this);
    m_actAddFavorite->setObjectName("addFavoriteAction");
    m_actAddFavorite->setShortcut(QKeySequence("Ctrl+D")); // Ctrl+D
    m_actRemoveFavorite = new QAction("取消收藏当前目录", this);
    m_actRemoveFavorite->setObjectName("removeFavoriteAction");
    fileMenu->addAction(m_actAddFavorite);
    fileMenu->addAction(m_actRemoveFavorite);

    fileMenu->addSeparator();
    fileMenu->addAction(m_actSaveWorkspace);
    fileMenu->addAction(m_actOpenWorkspace);
    fileMenu->addAction(m_actSaveProject);
    fileMenu->addAction(m_actOpenProject);
    m_actExportReport = new QAction("导出报告(&R)...", this);
    m_actExportImages = new QAction("导出图片(&E)...", this);
    fileMenu->addAction(m_actExportReport);
    fileMenu->addAction(m_actExportImages);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actExit);
}

void MainWindow::buildEditMenu(QMenuBar *menuBar)
{
    // ----- 编辑(&E) — A-10: Undo/Redo -----
    auto *editMenu = menuBar->addMenu("编辑(&E)");
    m_actUndo = new QAction("撤销(&U)", this);
    m_actUndo->setShortcut(QKeySequence::Undo); // Ctrl+Z
    m_actUndo->setEnabled(false);
    m_actRedo = new QAction("重做(&R)", this);
    m_actRedo->setShortcut(QKeySequence::Redo); // Ctrl+Y / Ctrl+Shift+Z
    m_actRedo->setEnabled(false);
    editMenu->addAction(m_actUndo);
    editMenu->addAction(m_actRedo);
    connect(m_actUndo, &QAction::triggered, this,
            [this]()
            {
                if (!m_cmdStack.undo())
                {
                    const std::string err = m_cmdStack.lastError();
                    if (!err.empty())
                        QMessageBox::warning(this, "撤销失败", QString::fromStdString(err));
                    updateUndoRedoActions();
                    return;
                }
                if (m_thumbnailPanel && !currentDir().isEmpty())
                    m_thumbnailPanel->setDirectory(currentDir());
                updateUndoRedoActions();
            });
    connect(m_actRedo, &QAction::triggered, this,
            [this]()
            {
                if (!m_cmdStack.redo())
                {
                    const std::string err = m_cmdStack.lastError();
                    if (!err.empty())
                        QMessageBox::warning(this, "重做失败", QString::fromStdString(err));
                    updateUndoRedoActions();
                    return;
                }
                if (m_thumbnailPanel && !currentDir().isEmpty())
                    m_thumbnailPanel->setDirectory(currentDir());
                updateUndoRedoActions();
            });
    m_cmdStack.setChangeCallback([this]() { updateUndoRedoActions(); });
}

void MainWindow::buildViewMenu(QMenuBar *menuBar)
{
    // ----- 视图(&V) -----
    auto *viewMenu = menuBar->addMenu("视图(&V)");
    m_actCompare = new QAction("比较模式(&C)", this);
    m_actToggleAnalysis = new QAction("分析面板(&H)", this);
    m_actToggleAnalysis->setObjectName("toggleAnalysisPanelAction");
    m_actToggleAnalysis->setCheckable(true);
    m_actToggleAnalysis->setChecked(false);
    // P0: in-session browse history (browser-style back/forward).
    m_actHistoryBack = new QAction("上一步(&B)", this);
    m_actHistoryBack->setObjectName("historyBackAction");
    m_actHistoryBack->setShortcut(QKeySequence::Back); // Alt+Left
    m_actHistoryForward = new QAction("下一步(&N)", this);
    m_actHistoryForward->setObjectName("historyForwardAction");
    m_actHistoryForward->setShortcut(QKeySequence::Forward); // Alt+Right
    // P0: Directory-level back/forward (independent of image history).
    m_actDirBack = new QAction("上一个目录", this);
    m_actDirBack->setObjectName("directoryBackAction");
    m_actDirBack->setShortcut(QKeySequence("Ctrl+Alt+Left"));
    m_actDirForward = new QAction("下一个目录", this);
    m_actDirForward->setObjectName("directoryForwardAction");
    m_actDirForward->setShortcut(QKeySequence("Ctrl+Alt+Right"));
    viewMenu->addAction(m_actHistoryBack);
    viewMenu->addAction(m_actHistoryForward);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actDirBack);
    viewMenu->addAction(m_actDirForward);
    viewMenu->addAction(m_actCompare);
    viewMenu->addAction(m_actToggleAnalysis);
    m_actToggleSearch = new QAction("全局搜索(&S)", this);
    m_actToggleSearch->setObjectName("toggleSearchPanelAction");
    m_actToggleSearch->setCheckable(true);
    m_actToggleSearch->setChecked(false);
    m_actToggleSearch->setShortcut(QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction(m_actToggleSearch);
    m_actFocusBrowse = new QAction("专注浏览模式 (Tab)", this);
    m_actFocusBrowse->setObjectName("focusBrowseAction");
    m_actFocusBrowse->setCheckable(true);
    m_actFocusBrowse->setShortcut(QKeySequence(Qt::Key_Tab));
    m_actFocusBrowse->setShortcutContext(Qt::WindowShortcut);
    viewMenu->addAction(m_actFocusBrowse);
    addAction(m_actFocusBrowse);
    m_actBrowseWorkspace = new QAction(tr("浏览布局"), this);
    m_actBrowseWorkspace->setObjectName("browseWorkspaceAction");
    m_actBrowseWorkspace->setCheckable(true);
    m_actBrowseWorkspace->setChecked(true);
    m_actBrowseWorkspace->setToolTip(tr("隐藏分析和搜索面板，保留文件夹与预览导航"));
    viewMenu->addAction(m_actBrowseWorkspace);
    m_actToggleMetadata = new QAction("图片信息(&I)", this);
    m_actToggleMetadata->setObjectName("toggleMetadataAction"); // stable test discovery
    m_actToggleMetadata->setCheckable(true);
    m_actToggleMetadata->setChecked(false);
    m_actToggleMetadata->setShortcut(QKeySequence("Ctrl+I"));
    viewMenu->addAction(m_actToggleMetadata);
    viewMenu->addSeparator();
    // Zoom commands act on the image viewer. Plain +/-/0/1 keys are handled
    // in keyPressEvent; the Ctrl variants live on the actions so they show
    // in the menu. Fit/Actual use plain 0/1 (a QAction plain-key shortcut
    // would shadow text entry in the search box).
    m_actZoomIn = new QAction("放大(&Z)", this);
    m_actZoomIn->setObjectName("zoomInAction");
    m_actZoomIn->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    m_actZoomOut = new QAction("缩小(&O)", this);
    m_actZoomOut->setObjectName("zoomOutAction");
    m_actZoomOut->setShortcut(QKeySequence("Ctrl+-"));
    m_actZoomFit = new QAction("适应窗口(&F) (0)", this);
    m_actZoomFit->setObjectName("zoomFitAction");
    m_actZoomActual = new QAction("实际大小(&A) (1)", this);
    m_actZoomActual->setObjectName("zoomActualAction");
    m_actFullscreen = new QAction("全屏(&U)", this);
    m_actFullscreen->setShortcut(QKeySequence("F11"));
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addAction(m_actZoomActual);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actFullscreen);
    m_actSlideshow = new QAction("幻灯片放映(&S) (S)", this);
    m_actSlideshow->setObjectName("slideshowAction");
    m_actSlideshow->setCheckable(true);
    viewMenu->addAction(m_actSlideshow);
}

void MainWindow::buildToolsHelpMenus(QMenuBar *menuBar)
{
    // ----- 工具(&T) -----
    auto *toolsMenu = menuBar->addMenu("工具(&T)");
    m_actBatch = new QAction("批量处理(&B)", this);
    m_actBatch->setShortcut(QKeySequence("Ctrl+Shift+B"));
    toolsMenu->addAction(m_actBatch);
    // M17: batch analyzer export — same path as gallery context menu.
    auto *actBatchAnalyze = new QAction(tr("批量分析导出(&A)..."), this);
    actBatchAnalyze->setShortcut(QKeySequence("Ctrl+Shift+A"));
    toolsMenu->addAction(actBatchAnalyze);
    connect(actBatchAnalyze, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->batchAnalyzeExport();
            });
    m_actPluginSettings = new QAction("插件管理(&P)...", this);
    toolsMenu->addAction(m_actPluginSettings);

    QAction *actPrefs = new QAction("首选项(&O)...", this);
    connect(actPrefs, &QAction::triggered, this, &MainWindow::openPreferences);
    toolsMenu->addAction(actPrefs);

    QAction *actOverlay = new QAction("分析叠加层/示波器...", this);
    connect(actOverlay, &QAction::triggered, this, &MainWindow::openAnalysisOverlay);
    toolsMenu->addAction(actOverlay);

    toolsMenu->addSeparator();
    m_actExportSettings = new QAction("导出设置(&E)...", this);
    m_actImportSettings = new QAction("导入设置(&I)...", this);
    toolsMenu->addAction(m_actExportSettings);
    toolsMenu->addAction(m_actImportSettings);
    toolsMenu->addSeparator();
    // M21: Memory Timeline snapshot (status bar + optional CSV dump).
    auto *actMemTimeline = new QAction("内存时间线(&M)...", this);
    actMemTimeline->setToolTip(tr("采样 MemoryTracker 时间线并显示峰值/最近样本"));
    connect(actMemTimeline, &QAction::triggered, this,
            [this]()
            {
                using mviewer::perf::MemoryTracker;
                auto &mt = MemoryTracker::instance();
                const auto snap = mt.sample();
                const auto hist = mt.timeline();
                QString msg = QString("Cache: %1 MB · Peak: %2 MB · Frames: %3 · Samples: %4")
                                  .arg(snap.cacheTotalBytes / (1024.0 * 1024.0), 0, 'f', 1)
                                  .arg(snap.peakBytes / (1024.0 * 1024.0), 0, 'f', 1)
                                  .arg(snap.liveImageFrames)
                                  .arg(hist.size());
                if (!hist.empty())
                {
                    // Compact sparkline of last ≤40 cache totals (▁..█).
                    size_t lo = SIZE_MAX, hi = 0;
                    const size_t n = hist.size();
                    const size_t start = n > 40 ? n - 40 : 0;
                    for (size_t i = start; i < n; ++i)
                    {
                        lo = std::min(lo, hist[i].cacheTotalBytes);
                        hi = std::max(hi, hist[i].cacheTotalBytes);
                    }
                    QString spark;
                    for (size_t i = start; i < n; ++i)
                    {
                        int lvl = 0;
                        if (hi > lo)
                            lvl = static_cast<int>((hist[i].cacheTotalBytes - lo) * 7 / (hi - lo));
                        lvl = std::clamp(lvl, 0, 7);
                        spark += QChar(0x2581 + lvl); // ▁▂▃▄▅▆▇█
                    }
                    msg += "\n" + spark;
                }
                statusBar()->showMessage(msg, 8000);
                QMessageBox::information(this, tr("内存时间线"), msg);
            });
    toolsMenu->addAction(actMemTimeline);

    // ----- 帮助(&H) -----
    auto *helpMenu = menuBar->addMenu("帮助(&H)");
    auto *actCheckUpdate = new QAction("检查更新...(&U)", this);
    connect(actCheckUpdate, &QAction::triggered, this, [this]() { checkForUpdates(false); });
    helpMenu->addAction(actCheckUpdate);
    helpMenu->addSeparator();
    auto *actShortcuts = new QAction("键盘快捷键(&K)", this);
    actShortcuts->setShortcut(QKeySequence(Qt::Key_F1));
    connect(actShortcuts, &QAction::triggered, this, &MainWindow::showShortcutsHelp);
    helpMenu->addAction(actShortcuts);
    m_actAbout = new QAction("关于(&A)", this);
    helpMenu->addAction(m_actAbout);
}

void MainWindow::buildBrowserShell()
{
    // FastStone-inspired browser shell: a compact, stable command strip sits
    // above the single editable path expression. The actions remain available
    // in menus as well, so the toolbar is an accelerator rather than a second
    // command model.
    m_actDirUp = new QAction(tr("上一级"), this);
    m_actDirUp->setObjectName("directoryUpAction");
    m_actRefresh = new QAction(tr("刷新"), this);
    m_actRefresh->setObjectName("refreshDirectoryAction");
    auto *browserToolBar = new QToolBar(tr("浏览工具栏"), this);
    addToolBar(Qt::TopToolBarArea, browserToolBar);
    browserToolBar->setObjectName("browserToolBar");
    browserToolBar->setMovable(false);
    browserToolBar->setFloatable(false);
    browserToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    browserToolBar->setIconSize(QSize(18, 18));
    auto addBrowserAction = [this, browserToolBar](QAction *action, QStyle::StandardPixmap icon)
    {
        action->setIcon(style()->standardIcon(icon));
        browserToolBar->addAction(action);
    };
    addBrowserAction(m_actOpenDir, QStyle::SP_DialogOpenButton);
    addBrowserAction(m_actDirBack, QStyle::SP_ArrowBack);
    addBrowserAction(m_actDirForward, QStyle::SP_ArrowForward);
    addBrowserAction(m_actDirUp, QStyle::SP_ArrowUp);
    addBrowserAction(m_actRefresh, QStyle::SP_BrowserReload);
    browserToolBar->addSeparator();
    addBrowserAction(m_actAddFavorite, QStyle::SP_DialogYesButton);
    addBrowserAction(m_actCompare, QStyle::SP_FileDialogDetailedView);
    addBrowserAction(m_actToggleAnalysis, QStyle::SP_FileDialogContentsView);
    addBrowserAction(m_actToggleSearch, QStyle::SP_FileDialogListView);
    browserToolBar->addSeparator();
    addBrowserAction(m_actBrowseWorkspace, QStyle::SP_DesktopIcon);

    // ----- Breadcrumb navigation bar (M15 Product Shell P0) -----
    m_breadcrumb = new BreadcrumbBar(this);
    // Keep the signal path for breadcrumb navigation, but do not spend a full
    // row duplicating the editable path field in the default browser shell.
    m_breadcrumb->setObjectName("breadcrumbBar");
    m_breadcrumb->hide();

    // ----- Path input bar (UX: type a path to jump to a directory) -----
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setObjectName("pathEdit");
    m_pathEdit->setPlaceholderText("输入目录路径并按 Enter 切换...");
    m_pathEdit->setToolTip("输入或粘贴目录路径，按 Enter 键进入该目录（等效于菜单\"打开目录\"）。");
    m_pathEdit->setClearButtonEnabled(true);
}

QWidget *MainWindow::buildNavigationPanel()
{
    // ----- Left column: favorites + filter + directory tree + preview -----
    auto *leftWidget = new QSplitter(Qt::Vertical, this);
    m_leftSplitter = leftWidget;
    m_navigationWidget = leftWidget;
    leftWidget->setObjectName("navigationPanel");

    // P0: Favorites bar — quick-access pinned directories above the tree.
    m_favoritesBar = new QListWidget(leftWidget);
    m_favoritesBar->setMaximumHeight(100);
    m_favoritesBar->hide();
    m_favoritesBar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_favoritesBar->setStyleSheet("QListWidget { background: #1e1e1e; border: none; }"
                                  "QListWidget::item { padding: 3px 8px; color: #ccc; }"
                                  "QListWidget::item:hover { background: #333; }");
    m_favoritesBar->setToolTip("收藏目录 — 右键移除，Ctrl+D 收藏当前目录");
    connect(m_favoritesBar, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
            { changeDirectory(item->data(Qt::UserRole).toString()); });
    m_favoritesBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_favoritesBar, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos)
            {
                auto *item = m_favoritesBar->itemAt(pos);
                if (!item)
                    return;
                QMenu menu;
                QAction *act = menu.addAction("移除收藏");
                if (menu.exec(m_favoritesBar->mapToGlobal(pos)) == act)
                    removeFavorite(item->data(Qt::UserRole).toString());
            });
    auto updateFavoritesVisibility = [this]()
    {
        if (m_favoritesBar)
            m_favoritesBar->setVisible(m_favoritesBar->count() > 0);
    };
    connect(m_favoritesBar->model(), &QAbstractItemModel::rowsInserted, this,
            [updateFavoritesVisibility]() { updateFavoritesVisibility(); });
    connect(m_favoritesBar->model(), &QAbstractItemModel::rowsRemoved, this,
            [updateFavoritesVisibility]() { updateFavoritesVisibility(); });
    leftWidget->addWidget(m_favoritesBar);

    // The vertical splitter owns only complete sections. Section internals use
    // layouts so a persisted splitter state can never stretch the filter edit
    // or either fixed-height title into a large blank area.
    auto *foldersSection = new QWidget(leftWidget);
    foldersSection->setObjectName("foldersSection");
    auto *foldersLayout = new QVBoxLayout(foldersSection);
    foldersLayout->setContentsMargins(0, 0, 0, 0);
    foldersLayout->setSpacing(2);
    auto *foldersLabel = new QLabel(tr("文件夹"), foldersSection);
    foldersLabel->setObjectName("foldersSectionLabel");
    foldersLabel->setProperty("sectionHeader", true);
    foldersLabel->setFixedHeight(24);
    foldersLayout->addWidget(foldersLabel);

    // P0: Directory name filter (placed between the section label and tree).
    m_directoryTree = new DirectoryTree(foldersSection);
    m_directoryTree->installEventFilter(this);
    m_directoryTree->filterEdit()->setMinimumHeight(26);
    m_directoryTree->filterEdit()->setMaximumHeight(34);
    foldersLayout->addWidget(m_directoryTree->filterEdit());
    foldersLayout->addWidget(m_directoryTree, 1);
    leftWidget->addWidget(foldersSection);

    auto *previewSection = new QWidget(leftWidget);
    previewSection->setObjectName("previewSection");
    auto *previewLayout = new QVBoxLayout(previewSection);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(2);
    auto *previewLabel = new QLabel(tr("预览"), previewSection);
    previewLabel->setObjectName("previewSectionLabel");
    previewLabel->setProperty("sectionHeader", true);
    previewLabel->setFixedHeight(24);
    previewLayout->addWidget(previewLabel);
    m_previewPanel = new PreviewPanel(previewSection);
    m_previewPanel->installEventFilter(this);
    previewLayout->addWidget(m_previewPanel, 1);
    leftWidget->addWidget(previewSection);

    leftWidget->setStretchFactor(0, 0); // optional favorites
    leftWidget->setStretchFactor(1, 3); // folders section
    leftWidget->setStretchFactor(2, 2); // preview section
    leftWidget->setChildrenCollapsible(false);
    leftWidget->setSizes({0, 390, 260});

    // The pre-professional-browser sidebar persisted a splitter with more
    // children. Qt can partially restore that state into the new three-section
    // splitter, producing a tiny folder tree and an oversized preview. Run once
    // after MainWindow's synchronous settings restore, then leave subsequent
    // user-adjusted proportions untouched.
    QTimer::singleShot(
        0, this,
        [this]()
        {
            constexpr int currentLayoutVersion = 1;
            QSettings settings;
            if (settings.value("browserSidebarLayoutVersion", 0).toInt() >= currentLayoutVersion)
            {
                return;
            }

            if (m_leftSplitter)
            {
                const bool showFavorites =
                    m_favoritesBar && m_favoritesBar->count() > 0 && !m_favoritesBar->isHidden();
                m_leftSplitter->setSizes({showFavorites ? 80 : 0, 390, 260});
            }
            settings.setValue("browserSidebarLayoutVersion", currentLayoutVersion);
            settings.sync();
        });

    return leftWidget;
}

QWidget *MainWindow::buildSortBar(QWidget *parent)
{
    auto *sortBar = new QWidget(parent);
    auto *sortRootLayout = new QVBoxLayout(sortBar);
    sortRootLayout->setContentsMargins(0, 0, 0, 0);
    sortRootLayout->setSpacing(2);
    auto *sortLayout = new QHBoxLayout;
    sortLayout->setContentsMargins(6, 4, 6, 4);
    sortRootLayout->addLayout(sortLayout);

    auto *advancedFilterPanel = new QWidget(sortBar);
    advancedFilterPanel->setObjectName("advancedFilterPanel");
    auto *advancedLayout = new QHBoxLayout(advancedFilterPanel);
    advancedLayout->setContentsMargins(6, 2, 6, 4);
    advancedLayout->setSpacing(6);
    sortRootLayout->addWidget(advancedFilterPanel);
    m_advancedFilterPanel = advancedFilterPanel;

    buildPrimarySortControls(sortBar, sortLayout);
    buildAdvancedFilterControls(sortBar, sortLayout, advancedFilterPanel, advancedLayout);
    buildSearchControls(sortBar, sortLayout, advancedFilterPanel, advancedLayout);
    return sortBar;
}

void MainWindow::buildPrimarySortControls(QWidget *sortBar, QHBoxLayout *sortLayout)
{
    sortLayout->addWidget(new QLabel("排序：", sortBar));
    m_sortCombo = new QComboBox(sortBar);
    m_sortCombo->addItem("文件名", ThumbnailPanel::SortName);
    m_sortCombo->addItem("日期", ThumbnailPanel::SortDate);
    m_sortCombo->addItem("大小", ThumbnailPanel::SortSize);
    m_sortCombo->addItem("分辨率", ThumbnailPanel::SortResolution);
    m_sortCombo->addItem("类型", ThumbnailPanel::SortType);   // A-2.2
    m_sortCombo->addItem("评分", ThumbnailPanel::SortRating); // A-2.2
    m_sortCombo->addItem("相机", ThumbnailPanel::SortCamera); // P0 #①
    m_sortCombo->addItem("镜头", ThumbnailPanel::SortLens);   // P0 #①
    sortLayout->addWidget(m_sortCombo);

    // A-2.2: sort direction toggle (ascending / descending).
    auto *sortDirBtn = new QPushButton("↑", sortBar);
    sortDirBtn->setFixedWidth(28);
    sortDirBtn->setCheckable(true);
    sortDirBtn->setToolTip("切换升序/降序");
    sortLayout->addWidget(sortDirBtn);
    connect(sortDirBtn, &QPushButton::toggled, this,
            [this, sortDirBtn](bool descending)
            {
                sortDirBtn->setText(descending ? "↓" : "↑");
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setSortAscending(!descending);
            });
}

void MainWindow::buildAdvancedFilterControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                                             QWidget *advancedFilterPanel,
                                             QHBoxLayout *advancedLayout)
{
    // A-2.3: file-type quick filter buttons.
    auto *typeFilterCombo = new QComboBox(sortBar);
    typeFilterCombo->setObjectName("typeFilterCombo");
    typeFilterCombo->addItem("全部类型", "");
    typeFilterCombo->addItem("JPG", "jpg,jpeg");
    typeFilterCombo->addItem("PNG", "png");
    typeFilterCombo->addItem("TIFF", "tif,tiff");
    typeFilterCombo->addItem("WebP", "webp");
    typeFilterCombo->addItem("RAW", "cr2,cr3,nef,nrw,arw,dng,orf,rw2,pef,raf");
    typeFilterCombo->setToolTip("按文件类型过滤");

    advancedLayout->addWidget(typeFilterCombo);
    auto *advancedFilterToggle = new QPushButton("高级筛选", sortBar);
    advancedFilterToggle->setObjectName("advancedFilterToggle");
    advancedFilterToggle->setCheckable(true);
    advancedFilterToggle->setToolTip("显示相机、镜头、评分和元数据筛选");
    sortLayout->addWidget(advancedFilterToggle);
    connect(advancedFilterToggle, &QPushButton::toggled, advancedFilterPanel, &QWidget::setVisible);
    auto *clearFilters = new QPushButton("清除筛选", sortBar);
    clearFilters->setObjectName("clearFiltersButton");
    advancedLayout->addWidget(clearFilters);

    // P0 #①: metadata filters — camera / lens (substring) and ISO (exact).
    auto *camEdit = new QLineEdit(sortBar);
    camEdit->setPlaceholderText("相机");
    camEdit->setFixedWidth(80);
    camEdit->setClearButtonEnabled(true);
    camEdit->setToolTip(tr("按相机(品牌/型号)过滤，子串匹配"));
    advancedLayout->addWidget(camEdit);
    auto *lensEdit = new QLineEdit(sortBar);
    lensEdit->setPlaceholderText("镜头");
    lensEdit->setFixedWidth(95);
    lensEdit->setClearButtonEnabled(true);
    lensEdit->setToolTip(tr("按镜头型号过滤，子串匹配"));
    advancedLayout->addWidget(lensEdit);
    auto *isoSpin = new QSpinBox(sortBar);
    isoSpin->setRange(0, 65535);
    isoSpin->setSpecialValueText("ISO");
    isoSpin->setToolTip(tr("按 ISO 精确过滤 (0 = 全部)"));
    isoSpin->setFixedWidth(72);
    advancedLayout->addWidget(isoSpin);
    connect(camEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setCameraFilter(t); });
    connect(lensEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setLensFilter(t); });
    connect(isoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { m_thumbnailPanel->setIsoFilter(v); });

    // P0 #①: free-form tag filter.
    auto *tagEdit = new QLineEdit(sortBar);
    tagEdit->setPlaceholderText("标签");
    tagEdit->setFixedWidth(90);
    tagEdit->setClearButtonEnabled(true);
    tagEdit->setToolTip(tr("按标签精确过滤 (空 = 全部)"));
    advancedLayout->addWidget(tagEdit);
    connect(tagEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setTagFilter(t); });

    connect(typeFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, typeFilterCombo]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setTypeFilter(typeFilterCombo->currentData().toString());
            });
    connect(clearFilters, &QPushButton::clicked, this,
            [typeFilterCombo, camEdit, lensEdit, isoSpin, tagEdit, this]()
            {
                typeFilterCombo->setCurrentIndex(0);
                camEdit->clear();
                lensEdit->clear();
                isoSpin->setValue(0);
                tagEdit->clear();
                m_searchEdit->clear();
                m_searchRecursive->setChecked(false);
                m_searchMeta->setChecked(false);
                m_ratingFilter->setCurrentIndex(0);
                m_flagFilter->setCurrentIndex(0);
            });
}

void MainWindow::buildSearchControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                                     QWidget *advancedFilterPanel, QHBoxLayout *advancedLayout)
{
    // P0-2: View mode switcher (Grid / Large / Small / Detail / Filmstrip / Compact)
    m_viewModeCombo = new QComboBox(sortBar);
    m_viewModeCombo->setObjectName("thumbnailViewModeCombo");
    m_viewModeCombo->addItem("网格", ThumbnailPanel::Thumbnail);
    m_viewModeCombo->addItem("大图标", ThumbnailPanel::LargeIcon);
    m_viewModeCombo->addItem("小图标", ThumbnailPanel::SmallIcon);
    m_viewModeCombo->addItem("列表", ThumbnailPanel::List);
    m_viewModeCombo->addItem("详情", ThumbnailPanel::Details);
    m_viewModeCombo->addItem("胶片条", ThumbnailPanel::Filmstrip);
    m_viewModeCombo->addItem("紧凑", ThumbnailPanel::Compact);
    m_viewModeCombo->setToolTip("切换缩略图视图模式（常用模式 Ctrl+1..6）");
    sortLayout->addWidget(m_viewModeCombo);
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                auto mode =
                    static_cast<ThumbnailPanel::ViewMode>(m_viewModeCombo->currentData().toInt());
                m_thumbnailPanel->setViewMode(mode);
            });

    // M15: Dynamic thumbnail size slider (48–512 px)
    sortLayout->addWidget(new QLabel("缩略图：", sortBar));
    m_thumbSizeSlider = new QSlider(Qt::Horizontal, sortBar);
    m_thumbSizeSlider->setObjectName("thumbnailSizeSlider");
    m_thumbSizeSlider->setRange(ThumbnailPanel::kMinThumbSize, ThumbnailPanel::kMaxThumbSize);
    m_thumbSizeSlider->setValue(ThumbnailPanel::kDefaultThumbSize);
    m_thumbSizeSlider->setFixedWidth(100);
    m_thumbSizeSlider->setToolTip("调整缩略图大小");
    sortLayout->addWidget(m_thumbSizeSlider);
    connect(m_thumbSizeSlider, &QSlider::valueChanged, this,
            [this](int value) { m_thumbnailPanel->setThumbSize(value); });
    // M18: live search bar.
    sortLayout->addWidget(new QLabel("搜索：", sortBar));
    m_searchEdit = new QLineEdit(sortBar);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText("按文件名过滤...");
    m_searchEdit->setClearButtonEnabled(true);
    sortLayout->addWidget(m_searchEdit, 1);
    m_searchRecursive = new QCheckBox("包含子目录", sortBar);
    advancedLayout->addWidget(m_searchRecursive);

    // P1: metadata-aware search (camera / lens / ISO / date / …).
    m_searchMeta = new QCheckBox("元数据", sortBar);
    advancedLayout->addWidget(m_searchMeta);

    // P1: star-rating filter.
    advancedLayout->addWidget(new QLabel("评分:", advancedFilterPanel));
    m_ratingFilter = new QComboBox(sortBar);
    m_ratingFilter->addItem("全部", 0);
    m_ratingFilter->addItem("★ 及以上", 1);
    m_ratingFilter->addItem("★★ 及以上", 2);
    m_ratingFilter->addItem("★★★ 及以上", 3);
    m_ratingFilter->addItem("★★★★ 及以上", 4);
    m_ratingFilter->addItem("★★★★★", 5);
    advancedLayout->addWidget(m_ratingFilter);

    // P3 tail: color label / reject / pick / recents filter.
    advancedLayout->addWidget(new QLabel("标记:", advancedFilterPanel));
    m_flagFilter = new QComboBox(sortBar);
    m_flagFilter->addItem("全部", 0);
    m_flagFilter->addItem("已收藏", 1);
    m_flagFilter->addItem("已拒绝", 2);
    m_flagFilter->addItem("最近浏览", 3);
    m_flagFilter->addItem("红标", 11);
    m_flagFilter->addItem("橙标", 12);
    m_flagFilter->addItem("黄标", 13);
    m_flagFilter->addItem("绿标", 14);
    m_flagFilter->addItem("蓝标", 15);
    m_flagFilter->addItem("紫标", 16);
    advancedLayout->addWidget(m_flagFilter);

    advancedLayout->addStretch(1);
    advancedFilterPanel->hide();
}

QWidget *MainWindow::buildGalleryPanel()
{
    // ----- Right column: sort bar (top) + image gallery -----
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    auto *sortBar = buildSortBar(rightWidget);
    rightLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(rightWidget);
    m_thumbnailPanel->setCommandStack(&m_cmdStack);   // A-10: reversible file ops
    m_thumbnailPanel->setSelectionModel(m_selection); // P0-2: gallery hover -> SSOT
    m_thumbnailPanel->installEventFilter(this);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // NOTE: clang-format 22.1.8 mis-parses the HTML '>" at a line break; the
    // text blocks below are format-guarded.
    // Empty-state hint: friendly call-to-action shown until the first
    // directory is opened (first-run guidance; hidden as soon as browsing
    // starts). Pure overlay: transparent for mouse events, so the gallery
    // beneath stays fully interactive.
    m_emptyState = new QLabel(m_thumbnailPanel);
    m_emptyState->setObjectName(QStringLiteral("emptyStateLabel"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setTextFormat(Qt::RichText);
    // clang-format off
    m_emptyState->setText(tr("<div style='color:#9aa0a6; font-size:16px;'>\u6253\u5f00\u4e00\u4e2a\u6587\u4ef6\u5939\u4ee5\u5f00\u59cb\u6d4f\u89c8</div>"
                             "<div style='color:#b0b4b8; font-size:12px; margin-top:8px;'>Ctrl+O \u6253\u5f00\u76ee\u5f55 \u00b7 \u4e5f\u53ef\u5c06\u56fe\u7247\u6216\u6587\u4ef6\u5939\u62d6\u5165\u7a97\u53e3</div>"));
    // clang-format on
    m_emptyState->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyState->show();
    updateEmptyState();

    // Empty-folder hint: a directory is open but nothing is displayable
    // (no image files, or every entry is hidden by filters). Deferred via
    // a short timer so the transient pre-scan zero cannot flash it.
    m_emptyFolderLabel = new QLabel(m_thumbnailPanel);
    m_emptyFolderLabel->setObjectName(QStringLiteral("emptyFolderLabel"));
    m_emptyFolderLabel->setAlignment(Qt::AlignCenter);
    m_emptyFolderLabel->setWordWrap(true);
    m_emptyFolderLabel->setTextFormat(Qt::RichText);
    // clang-format off
    m_emptyFolderLabel->setText(tr("<div style='color:#9aa0a6; font-size:15px;'>\u6b64\u6587\u4ef6\u5939\u4e2d\u6ca1\u6709\u53ef\u663e\u793a\u7684\u56fe\u7247</div>"
                                 "<div style='color:#b0b4b8; font-size:12px; margin-top:8px;'>\u5c1d\u8bd5\u6e05\u9664\u7b5b\u9009\u6216\u6362\u4e00\u4e2a\u6587\u4ef6\u5939</div>"));
    // clang-format on
    m_emptyFolderLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyFolderLabel->hide();
    m_emptyFolderTimer = new QTimer(this);
    m_emptyFolderTimer->setSingleShot(true);
    m_emptyFolderTimer->setInterval(600);
    connect(m_emptyFolderTimer, &QTimer::timeout, this, &MainWindow::updateEmptyFolderState);
    connect(m_thumbnailPanel, &ThumbnailPanel::viewModeChanged, this,
            [this](ThumbnailPanel::ViewMode mode)
            {
                for (int i = 0; i < m_viewModeCombo->count(); ++i)
                {
                    if (m_viewModeCombo->itemData(i).toInt() == static_cast<int>(mode))
                    {
                        const QSignalBlocker blocker(m_viewModeCombo);
                        m_viewModeCombo->setCurrentIndex(i);
                        break;
                    }
                }
                const bool adjustable = mode == ThumbnailPanel::Thumbnail ||
                                        mode == ThumbnailPanel::Filmstrip ||
                                        mode == ThumbnailPanel::Compact;
                m_thumbSizeSlider->setEnabled(adjustable);
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::thumbSizeChanged, this,
            [this](int size)
            {
                if (!m_thumbSizeSlider)
                    return;
                const QSignalBlocker blocker(m_thumbSizeSlider);
                m_thumbSizeSlider->setValue(size);
            });
    m_thumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());

    return rightWidget;
}

void MainWindow::buildAnalysisAndSearchPanels()
{
    // ----- Analysis panel (rightmost) + Metadata panel (M18, between gallery & analysis) -----
    m_analysisPanel = new AnalysisPanel(this);
    m_analysisPanel->setObjectName("analysisPanel");
    m_analysisPanel->installEventFilter(this);
    // M21: AnalysisPanel ↔ AnalyzerModel (history / pin / result SSOT).
    m_analysisPanel->setAnalyzerModel(m_analyzer);
    connect(m_analysisPanel, &AnalysisPanel::historyImageRequested, this,
            [this](const QString &path)
            {
                if (!path.isEmpty())
                    onImageOpen(path);
            });
    // A-7.2: plugins are loaded in main() before MainWindow; refresh the combo
    // so runtime-discovered analyzers appear immediately.
    m_analysisPanel->refreshAnalyzers();
    // M15 P0#3: inject the analyzer pipeline so the panel orchestrates analyzers
    // through it instead of reaching the registry directly. MainWindow never
    // lists or creates analyzers itself — the pipeline owns that responsibility.
    m_analysisPanel->setPipeline(std::make_unique<AnalyzerPipeline>());
    // P1-6: expose a one-click report export from inside the analysis panel.
    connect(m_analysisPanel, &AnalysisPanel::exportRequested, this, [this]() { exportReport(); });
    m_metadataPanel = new MetadataPanel(this);
    m_searchPanel = new SearchPanel(this);
    m_searchPanel->setObjectName("searchPanel");
    m_searchPanel->installEventFilter(this);
}

void MainWindow::buildCentralContainer(QWidget *leftWidget, QWidget *rightWidget)
{
    // ----- 4-way horizontal split: left | gallery | analysis | search -----
    // Metadata panel is an overlay — hidden by default, shown on image click.
    auto *centralSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter = centralSplitter;
    centralSplitter->addWidget(leftWidget);
    centralSplitter->addWidget(rightWidget);
    centralSplitter->addWidget(m_analysisPanel);
    centralSplitter->addWidget(m_searchPanel);
    centralSplitter->setStretchFactor(0, 0);
    centralSplitter->setStretchFactor(1, 1);
    centralSplitter->setStretchFactor(2, 0);
    centralSplitter->setStretchFactor(3, 0);
    centralSplitter->setSizes({340, 820, 300, 240});
    // Prevent any panel from being collapsed to zero width — keeps the layout
    // usable when the window is narrow.
    centralSplitter->setChildrenCollapsible(false);
    leftWidget->setMinimumWidth(200);
    rightWidget->setMinimumWidth(320);
    m_analysisPanel->setMinimumWidth(200);
    m_searchPanel->setMinimumWidth(180);
    m_analysisPanel->hide();
    m_searchPanel->hide();
    // ----- M15: main content wrapper (breadcrumb + splitter) -----
    auto *mainContainer = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_breadcrumb);
    mainLayout->addWidget(m_pathEdit);
    mainLayout->addWidget(centralSplitter, 1);
    setCentralWidget(mainContainer);
}

void MainWindow::buildMetadataPanelUi()
{
    // ----- Metadata overlay (A-5: floating tool window, default hidden) -----
    // Not in the splitter — floats over the main area so browsing is unobstructed.
    m_metadataPanel->setParent(this);
    m_metadataPanel->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    m_metadataPanel->setAttribute(Qt::WA_ShowWithoutActivating, false);
    m_metadataPanel->setFixedSize(300, 480);
    m_metadataPanel->setWindowOpacity(0.92); // semi-transparent
    m_metadataPanel->setStyleSheet("MetadataPanel { background: rgba(30,30,30,220); color: #eee; "
                                   "border: 1px solid #555; border-radius: 6px; }");
    m_metadataPanel->hide();
}

void MainWindow::buildImageViewerUi()
{
    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle("图片查看 - MViewer");

    // P0-3: metadata overlay on the image viewer (toggle with I / M / 图片信息)
    m_metadataOverlay = new MetadataOverlay(m_imageViewer);
    m_metadataOverlay->hide();
    // P0-3: the overlay can close itself (ESC / I / M / click). Mirror any
    // visibility change back into the "图片信息" menu toggle so all entry points
    // stay consistent and a closed overlay does not silently re-open on the next
    // image selection. Every show/hide also drives the async histogram task:
    // showing schedules (once, latest-wins), hiding cancels the in-flight work.
    connect(m_metadataOverlay, &MetadataOverlay::visibilityChanged, this,
            [this](bool visible)
            {
                if (visible)
                    scheduleMetadataHistogram();
                else
                    cancelMetadataHistogram();
                if (m_actToggleMetadata)
                    m_actToggleMetadata->setChecked(visible);
            });

    m_imageViewer->installEventFilter(this);
    m_imageViewer->setMouseTracking(true);
    m_metadataHoverTimer = new QTimer(this);
    m_metadataHoverTimer->setSingleShot(true);
    m_metadataHoverTimer->setInterval(600);
}

void MainWindow::buildStatusBarUi()
{
    // P0 #①: real-time status bar — image count, total/selected size, viewer
    // zoom, and live cache hit-rate. Persistent (not transient showMessage).
    m_lblImage = new QLabel("—", this);
    m_lblCount = new QLabel("图片 0", this);
    m_lblSize = new QLabel("大小 0 B", this);
    m_lblZoom = new QLabel("缩放 —", this);
    m_lblCache = new QLabel("命中率 —", this);
    for (QLabel *l : {m_lblImage, m_lblCount, m_lblSize, m_lblZoom, m_lblCache})
        l->setContentsMargins(8, 0, 8, 0);
    statusBar()->addPermanentWidget(m_lblImage);
    statusBar()->addPermanentWidget(m_lblCount);
    statusBar()->addPermanentWidget(m_lblSize);
    statusBar()->addPermanentWidget(m_lblZoom);
    statusBar()->addPermanentWidget(m_lblCache);

    connect(m_thumbnailPanel, &ThumbnailPanel::statsChanged, this,
            [this](int total, qint64 totalBytes, int selected, qint64 selBytes)
            {
                m_lblCount->setText(QString("图片 %1").arg(total));
                if (selected > 0)
                    m_lblSize->setText(
                        QString("已选 %1 · %2").arg(selected).arg(formatBytes(selBytes)));
                else
                    m_lblSize->setText(QString("大小 %1").arg(formatBytes(totalBytes)));
            });
    connect(m_imageViewer, &ImageViewer::zoomChanged, this,
            [this](int pct) { m_lblZoom->setText(QString("缩放 %1%").arg(pct)); });

    m_statTimer = new QTimer(this);
    connect(m_statTimer, &QTimer::timeout, this, &MainWindow::updateCacheStat);
    m_statTimer->start(500);

    // A-3.4: initial enablement for selection-dependent actions.
    updateSelectionActions();

    statusBar()->showMessage("就绪");
}
