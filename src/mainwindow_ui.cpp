// MainWindow UI construction: widgets, menus, docks, status bar (M20 P0#1).
#include "mainwindow_p.h"

void MainWindow::setupUi()
{
    auto *menuBar = new QMenuBar(this);

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
    m_actAddFavorite->setShortcut(QKeySequence("Ctrl+D")); // Ctrl+D
    m_actRemoveFavorite = new QAction("取消收藏当前目录", this);
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

    // ----- 视图(&V) -----
    auto *viewMenu = menuBar->addMenu("视图(&V)");
    m_actCompare = new QAction("比较模式(&C)", this);
    m_actToggleAnalysis = new QAction("直方图(&H)", this);
    m_actToggleAnalysis->setCheckable(true);
    m_actToggleAnalysis->setChecked(true);
    // P0: in-session browse history (browser-style back/forward).
    m_actHistoryBack = new QAction("上一步(&B)", this);
    m_actHistoryBack->setShortcut(QKeySequence::Back); // Alt+Left
    m_actHistoryForward = new QAction("下一步(&N)", this);
    m_actHistoryForward->setShortcut(QKeySequence::Forward); // Alt+Right
    // P0: Directory-level back/forward (independent of image history).
    m_actDirBack = new QAction("上一个目录", this);
    m_actDirBack->setShortcut(QKeySequence("Ctrl+Alt+Left"));
    m_actDirForward = new QAction("下一个目录", this);
    m_actDirForward->setShortcut(QKeySequence("Ctrl+Alt+Right"));
    viewMenu->addAction(m_actHistoryBack);
    viewMenu->addAction(m_actHistoryForward);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actDirBack);
    viewMenu->addAction(m_actDirForward);
    viewMenu->addAction(m_actCompare);
    viewMenu->addAction(m_actToggleAnalysis);
    m_actToggleSearch = new QAction("全局搜索(&S)", this);
    m_actToggleSearch->setCheckable(true);
    m_actToggleSearch->setChecked(true);
    m_actToggleSearch->setShortcut(QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction(m_actToggleSearch);
    m_actToggleMetadata = new QAction("图片信息(&I)", this);
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
    m_actZoomIn->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    m_actZoomOut = new QAction("缩小(&O)", this);
    m_actZoomOut->setShortcut(QKeySequence("Ctrl+-"));
    m_actZoomFit = new QAction("适应窗口(&F) (0)", this);
    m_actZoomActual = new QAction("实际大小(&A) (1)", this);
    m_actFullscreen = new QAction("全屏(&U)", this);
    m_actFullscreen->setShortcut(QKeySequence("F11"));
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addAction(m_actZoomActual);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actFullscreen);
    m_actSlideshow = new QAction("幻灯片放映(&S) (S)", this);
    m_actSlideshow->setCheckable(true);
    viewMenu->addAction(m_actSlideshow);

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

    setMenuBar(menuBar);

    // ----- Breadcrumb navigation bar (M15 Product Shell P0) -----
    m_breadcrumb = new BreadcrumbBar(this);

    // ----- Path input bar (UX: type a path to jump to a directory) -----
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("输入目录路径并按 Enter 切换...");
    m_pathEdit->setToolTip("输入或粘贴目录路径，按 Enter 键进入该目录（等效于菜单\"打开目录\"）。");
    m_pathEdit->setClearButtonEnabled(true);

    // ----- Left column: favorites + filter + directory tree + preview -----
    auto *leftWidget = new QSplitter(Qt::Vertical, this);
    m_leftSplitter = leftWidget;

    // P0: Favorites bar — quick-access pinned directories above the tree.
    m_favoritesBar = new QListWidget(leftWidget);
    m_favoritesBar->setMaximumHeight(100);
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
    leftWidget->addWidget(m_favoritesBar);

    // P0: Directory name filter (placed between favorites and tree).
    m_directoryTree = new DirectoryTree(leftWidget);
    m_directoryTree->installEventFilter(this);
    leftWidget->addWidget(m_directoryTree->filterEdit());
    leftWidget->addWidget(m_directoryTree);
    m_previewPanel = new PreviewPanel(leftWidget);
    m_previewPanel->installEventFilter(this);

    leftWidget->addWidget(m_previewPanel);
    leftWidget->setStretchFactor(1, 0); // filter
    leftWidget->setStretchFactor(2, 3); // tree
    leftWidget->setStretchFactor(3, 2); // preview
    leftWidget->setChildrenCollapsible(false);
    leftWidget->setSizes({80, 28, 320, 200});

    // ----- Right column: sort bar (top) + image gallery -----
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    auto *sortBar = new QWidget(rightWidget);
    auto *sortLayout = new QHBoxLayout(sortBar);
    sortLayout->setContentsMargins(6, 4, 6, 4);
    sortLayout->addWidget(new QLabel("排序：", sortBar));
    auto *sortCombo = new QComboBox(sortBar);
    m_sortCombo = sortCombo;
    sortCombo->addItem("文件名", ThumbnailPanel::SortName);
    sortCombo->addItem("日期", ThumbnailPanel::SortDate);
    sortCombo->addItem("大小", ThumbnailPanel::SortSize);
    sortCombo->addItem("分辨率", ThumbnailPanel::SortResolution);
    sortCombo->addItem("类型", ThumbnailPanel::SortType);   // A-2.2
    sortCombo->addItem("评分", ThumbnailPanel::SortRating); // A-2.2
    sortCombo->addItem("相机", ThumbnailPanel::SortCamera); // P0 #①
    sortCombo->addItem("镜头", ThumbnailPanel::SortLens);   // P0 #①
    sortLayout->addWidget(sortCombo);

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

    // A-2.3: file-type quick filter buttons.
    auto *typeFilterCombo = new QComboBox(sortBar);
    typeFilterCombo->addItem("全部类型", "");
    typeFilterCombo->addItem("JPG", "jpg,jpeg");
    typeFilterCombo->addItem("PNG", "png");
    typeFilterCombo->addItem("TIFF", "tif,tiff");
    typeFilterCombo->addItem("WebP", "webp");
    typeFilterCombo->addItem("RAW", "cr2,cr3,nef,nrw,arw,dng,orf,rw2,pef,raf");
    typeFilterCombo->setToolTip("按文件类型过滤");
    sortLayout->addWidget(typeFilterCombo);

    // P0 #①: metadata filters — camera / lens (substring) and ISO (exact).
    auto *camEdit = new QLineEdit(sortBar);
    camEdit->setPlaceholderText("相机");
    camEdit->setFixedWidth(80);
    camEdit->setClearButtonEnabled(true);
    camEdit->setToolTip(tr("按相机(品牌/型号)过滤，子串匹配"));
    sortLayout->addWidget(camEdit);
    auto *lensEdit = new QLineEdit(sortBar);
    lensEdit->setPlaceholderText("镜头");
    lensEdit->setFixedWidth(95);
    lensEdit->setClearButtonEnabled(true);
    lensEdit->setToolTip(tr("按镜头型号过滤，子串匹配"));
    sortLayout->addWidget(lensEdit);
    auto *isoSpin = new QSpinBox(sortBar);
    isoSpin->setRange(0, 65535);
    isoSpin->setSpecialValueText("ISO");
    isoSpin->setToolTip(tr("按 ISO 精确过滤 (0 = 全部)"));
    isoSpin->setFixedWidth(72);
    sortLayout->addWidget(isoSpin);
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
    sortLayout->addWidget(tagEdit);
    connect(tagEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setTagFilter(t); });

    connect(typeFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, typeFilterCombo]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setTypeFilter(typeFilterCombo->currentData().toString());
            });

    // P0-2: View mode switcher (Grid / Large / Small / Detail / Filmstrip / Compact)
    auto *viewModeCombo = new QComboBox(sortBar);
    viewModeCombo->addItem("网格", ThumbnailPanel::Thumbnail);
    viewModeCombo->addItem("大图标", ThumbnailPanel::LargeIcon);
    viewModeCombo->addItem("小图标", ThumbnailPanel::SmallIcon);
    viewModeCombo->addItem("列表", ThumbnailPanel::List);
    viewModeCombo->addItem("详情", ThumbnailPanel::Details);
    viewModeCombo->addItem("胶片条", ThumbnailPanel::Filmstrip);
    viewModeCombo->addItem("紧凑", ThumbnailPanel::Compact);
    viewModeCombo->setToolTip("切换缩略图视图模式 (Ctrl+1..6)");
    sortLayout->addWidget(viewModeCombo);
    connect(viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, viewModeCombo]()
            {
                auto mode =
                    static_cast<ThumbnailPanel::ViewMode>(viewModeCombo->currentData().toInt());
                m_thumbnailPanel->setViewMode(mode);
            });

    // M15: Dynamic thumbnail size slider (48–512 px)
    sortLayout->addWidget(new QLabel("缩略图：", sortBar));
    m_thumbSizeSlider = new QSlider(Qt::Horizontal, sortBar);
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
    m_searchEdit->setPlaceholderText("按文件名过滤...");
    m_searchEdit->setClearButtonEnabled(true);
    sortLayout->addWidget(m_searchEdit, 1);
    m_searchRecursive = new QCheckBox("包含子目录", sortBar);
    sortLayout->addWidget(m_searchRecursive);

    // P1: metadata-aware search (camera / lens / ISO / date / …).
    m_searchMeta = new QCheckBox("元数据", sortBar);
    sortLayout->addWidget(m_searchMeta);

    // P1: star-rating filter.
    sortLayout->addWidget(new QLabel("评分:", sortBar));
    m_ratingFilter = new QComboBox(sortBar);
    m_ratingFilter->addItem("全部", 0);
    m_ratingFilter->addItem("★ 及以上", 1);
    m_ratingFilter->addItem("★★ 及以上", 2);
    m_ratingFilter->addItem("★★★ 及以上", 3);
    m_ratingFilter->addItem("★★★★ 及以上", 4);
    m_ratingFilter->addItem("★★★★★", 5);
    sortLayout->addWidget(m_ratingFilter);

    // P3 tail: color label / reject / pick / recents filter.
    sortLayout->addWidget(new QLabel("标记:", sortBar));
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
    sortLayout->addWidget(m_flagFilter);

    sortLayout->addStretch(0);
    rightLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(rightWidget);
    m_thumbnailPanel->setCommandStack(&m_cmdStack); // A-10: reversible file ops
    m_thumbnailPanel->installEventFilter(this);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // ----- Analysis panel (rightmost) + Metadata panel (M18, between gallery & analysis) -----
    m_analysisPanel = new AnalysisPanel(this);
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
    m_searchPanel->installEventFilter(this);

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
    // ----- M15: main content wrapper (breadcrumb + splitter) -----
    auto *mainContainer = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_breadcrumb);
    mainLayout->addWidget(m_pathEdit);
    mainLayout->addWidget(centralSplitter, 1);
    setCentralWidget(mainContainer);

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

    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle("图片查看 - MViewer");

    // P0-3: metadata overlay on the image viewer (toggle with 'M' key / click / hover)
    m_metadataOverlay = new MetadataOverlay(m_imageViewer);
    m_metadataOverlay->hide();
    // P0-3: the overlay can close itself (ESC / I / M / click). Mirror any
    // visibility change back into the "图片信息" menu toggle so all entry points
    // stay consistent and a closed overlay does not silently re-open on the next
    // image selection.
    connect(m_metadataOverlay, &MetadataOverlay::visibilityChanged, this,
            [this](bool visible)
            {
                if (m_actToggleMetadata)
                    m_actToggleMetadata->setChecked(visible);
            });

    // Intercept viewer mouse events to show overlay on click / hover.
    m_imageViewer->installEventFilter(this);
    m_imageViewer->setMouseTracking(true);
    m_metadataHoverTimer = new QTimer(this);
    m_metadataHoverTimer->setSingleShot(true);
    m_metadataHoverTimer->setInterval(600);
    connect(m_metadataHoverTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!currentImagePath().isEmpty())
                    showMetadataOverlay();
            });

    // ----- Signals -----
    connect(m_directoryTree, &DirectoryTree::directoryChanged, m_thumbnailPanel,
            &ThumbnailPanel::setDirectory);
    connect(m_breadcrumb, &BreadcrumbBar::pathSelected, this, &MainWindow::onBreadcrumbPath);
    connect(m_directoryTree, &DirectoryTree::directoryChanged, this,
            [this](const QString &path)
            {
                m_breadcrumb->setPath(path); // M15: update breadcrumb bar
                if (m_pathEdit)
                    m_pathEdit->setText(QDir::toNativeSeparators(path));
                // M19: DirectoryModel + ImageListModel are the SSOT.
                m_directory->setCurrentDirectory(path);
                m_workspace->setRootPath(path);
                QStringList paths;
                for (const auto &p : OpenDirectoryUseCase::execute(path.toStdString()).imagePaths)
                    paths.append(QString::fromStdString(p));
                m_imageList->setPaths(paths, path);
                // P0-1: record this folder in the recent-folders LRU + repopulate
                // the Recent menu.
                m_recent.add(path.toStdString());
                m_appState.addRecentFolder(path);
                m_directory->addRecentFolder(path);
                rebuildRecentMenu();
                // P0: push directory-level history for back/forward navigation.
                pushDirHistory(path);
                const int n = m_imageList->count();
                statusBar()->showMessage(QString("目录: %1, 图片数: %2").arg(path).arg(n));
                // With no image selected yet, the title carries the folder.
                if (currentImagePath().isEmpty())
                    setWindowTitle(QString("%1 - MViewer").arg(QDir(path).dirName()));
                scheduleReindex();
            });

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

    // EventBus (decoupled, dual-mode) subscriptions.
    EventBus::instance().subscribe("image.open",
                                   [this](void *ctx)
                                   {
                                       auto *path = static_cast<QString *>(ctx);
                                       if (path)
                                           onImageOpen(*path);
                                   });
    EventBus::instance().subscribe("compare.requested",
                                   [this](void *ctx)
                                   {
                                       auto *paths = static_cast<QStringList *>(ctx);
                                       if (paths)
                                           openCompare(*paths);
                                   });

    connect(m_imageViewer, &ImageViewer::regionStats, m_analysisPanel,
            &AnalysisPanel::setRegionStats);
    // P0续: feed the decoded ImageFrame to the analysis panel once the async
    // load completes (no re-decode on the UI thread). This replaces the old
    // synchronous QImage(path) decode that blocked browsing.
    connect(m_imageViewer, &ImageViewer::imageReady, m_analysisPanel, &AnalysisPanel::setFrame);
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
    // A-7.3: viewer context-menu "分析" → show panel + run through unified entry.
    connect(m_imageViewer, &ImageViewer::analysisRequested, this,
            [this](const QString &analyzerId)
            {
                if (!m_analysisPanel)
                    return;
                m_analysisPanel->setVisible(true);
                m_analysisPanel->runAnalyzer(analyzerId);
            });
    connect(
        m_imageViewer, &ImageViewer::pixelInfo, this,
        [this](int x, int y, int r, int g, int b, int a, bool valid)
        {
            if (valid)
            {
                if (a < 255)
                    statusBar()->showMessage(QString("像素 [%1,%2]  RGBA(%3,%4,%5,%6)")
                                                 .arg(x)
                                                 .arg(y)
                                                 .arg(r)
                                                 .arg(g)
                                                 .arg(b)
                                                 .arg(a));
                else
                    statusBar()->showMessage(
                        QString("像素 [%1,%2]  RGB(%3,%4,%5)").arg(x).arg(y).arg(r).arg(g).arg(b));
            }
            else
            {
                statusBar()->showMessage("光标不在图像上");
            }
            m_analysisPanel->showPixel(x, y, r, g, b, valid);
        });

    connect(sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, sortCombo](int)
            {
                m_thumbnailPanel->setSortMode(
                    static_cast<ThumbnailPanel::SortMode>(sortCombo->currentData().toInt()));
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

    // ----- Menu actions -----
    connect(m_actOpenDir, &QAction::triggered, this,
            [this]()
            {
                const QString dir = QFileDialog::getExistingDirectory(this, "打开目录");
                if (!dir.isEmpty())
                    changeDirectory(dir);
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
                const QString file = QFileDialog::getOpenFileName(
                    this, "打开图片", currentDir().isEmpty() ? QString() : currentDir(),
                    "图片文件 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp"
                    " *.cr2 *.cr3 *.nef *.nrw *.arw *.dng *.orf *.rw2 *.pef *.raf);;"
                    "所有文件 (*)");
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
    connect(m_actToggleAnalysis, &QAction::triggered, m_analysisPanel, &QWidget::setVisible);
    connect(m_actToggleSearch, &QAction::triggered, m_searchPanel, &QWidget::setVisible);
    // P0-3 / A-5: metadata toggle — show both the viewer overlay AND the floating
    // MetadataPanel (positioned on the right edge of the main window).
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
                        m_metadataPanel->setImage(currentImagePath());
                        positionMetadataPanel();
                        m_metadataPanel->show();
                        m_metadataPanel->raise();
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
        { QMessageBox::about(this, "关于 MViewer", "MViewer\n\n一个简单的图片查看与分析工具。"); });

    // P0: recent / favorites / history wiring.
    connect(m_actAddFavorite, &QAction::triggered, this, &MainWindow::addFavoriteCurrent);
    connect(m_actRemoveFavorite, &QAction::triggered, this, [this]() { removeFavorite(); });
    connect(m_actHistoryBack, &QAction::triggered, this, [this]() { navigateHistory(-1); });
    connect(m_actHistoryForward, &QAction::triggered, this, [this]() { navigateHistory(1); });
    connect(m_actDirBack, &QAction::triggered, this, &MainWindow::goDirBack);
    connect(m_actDirForward, &QAction::triggered, this, &MainWindow::goDirForward);

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
