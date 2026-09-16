// MainWindow menu construction (M20 P0#1).
#include "mainwindow_p.h"

#include "display/DisplayColorContextProvider.h"

#include <QIcon>
#include <QMenuBar>

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

    editMenu->addSeparator();
    m_actRotateCW = new QAction(tr("顺时针旋转 90°(&R)"), this);
    m_actRotateCW->setObjectName("rotateCWAction");
    m_actRotateCW->setShortcut(QKeySequence("Ctrl+R"));
    m_actRotateCCW = new QAction(tr("逆时针旋转 90°(&L)"), this);
    m_actRotateCCW->setObjectName("rotateCCWAction");
    m_actRotateCCW->setShortcut(QKeySequence("Ctrl+Shift+R"));
    editMenu->addAction(m_actRotateCW);
    editMenu->addAction(m_actRotateCCW);
    connect(m_actRotateCW, &QAction::triggered, this,
            [this]()
            {
                if (m_compareView && m_compareView->isVisible())
                    m_compareView->rotateCurrentCell(90);
                else if (m_imageViewer)
                    m_imageViewer->rotateCW();
            });
    connect(m_actRotateCCW, &QAction::triggered, this,
            [this]()
            {
                if (m_compareView && m_compareView->isVisible())
                    m_compareView->rotateCurrentCell(-90);
                else if (m_imageViewer)
                    m_imageViewer->rotateCCW();
            });
    m_actFlipH = new QAction(tr("水平翻转(&H)"), this);
    m_actFlipH->setObjectName("flipHAction");
    m_actFlipH->setShortcut(QKeySequence("Ctrl+Shift+H"));
    m_actFlipV = new QAction(tr("垂直翻转(&V)"), this);
    m_actFlipV->setObjectName("flipVAction");
    m_actFlipV->setShortcut(QKeySequence("Ctrl+Shift+V"));
    editMenu->addAction(m_actFlipH);
    editMenu->addAction(m_actFlipV);
    connect(m_actFlipH, &QAction::triggered, this,
            [this]()
            {
                if (m_compareView && m_compareView->isVisible())
                    m_compareView->flipCurrentCell(true);
                else if (m_imageViewer)
                    m_imageViewer->flipHorizontal();
            });
    connect(m_actFlipV, &QAction::triggered, this,
            [this]()
            {
                if (m_compareView && m_compareView->isVisible())
                    m_compareView->flipCurrentCell(false);
                else if (m_imageViewer)
                    m_imageViewer->flipVertical();
            });

    editMenu->addSeparator();
    m_actSelectAll = new QAction(tr("全选(&A)"), this);
    m_actSelectAll->setObjectName("selectAllAction");
    m_actSelectAll->setShortcut(QKeySequence::SelectAll);
    m_actDeselectAll = new QAction(tr("取消选择(&D)"), this);
    m_actDeselectAll->setObjectName("deselectAllAction");
    m_actDeselectAll->setShortcut(QKeySequence("Ctrl+Shift+A"));
    m_actInvertSelection = new QAction(tr("反向选择(&I)"), this);
    m_actInvertSelection->setObjectName("invertSelectionAction");
    m_actInvertSelection->setShortcut(QKeySequence("Ctrl+Shift+I"));
    editMenu->addAction(m_actSelectAll);
    editMenu->addAction(m_actDeselectAll);
    editMenu->addAction(m_actInvertSelection);
    connect(m_actSelectAll, &QAction::triggered, this,
            [this]() { if (m_thumbnailPanel) m_thumbnailPanel->selectAll(); });
    connect(m_actDeselectAll, &QAction::triggered, this,
            [this]() { if (m_thumbnailPanel) m_thumbnailPanel->clearSelection(); });
    connect(m_actInvertSelection, &QAction::triggered, this,
            [this]() { if (m_thumbnailPanel) m_thumbnailPanel->invertSelection(); });
}

void MainWindow::buildViewMenu(QMenuBar *menuBar)
{
    // ----- 视图(&V) -----
    auto *viewMenu = menuBar->addMenu("视图(&V)");
    m_actCompare = new QAction("比较模式(&C)", this);
    m_actCompare->setToolTip(tr("选中 2–8 张图片后按 P 或 C 打开比较"));
    m_actToggleAnalysis = new QAction("分析面板(&H)", this);
    m_actToggleAnalysis->setObjectName("toggleAnalysisPanelAction");
    m_actToggleAnalysis->setCheckable(true);
    m_actToggleAnalysis->setChecked(false);
    m_actToggleAnalysis->setShortcut(QKeySequence(QStringLiteral("Alt+H")));
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
    m_zoomPresetsMenu = viewMenu->addMenu("缩放预设");
    m_zoomPresetsMenu->setObjectName("zoomPresetsMenu");
    m_zoomPresetsMenu->addAction("50%", this, [this]() { zoomViewer(4); });
    m_zoomPresetsMenu->addAction("100% (实际大小)", this, [this]() { zoomViewer(3); });
    m_zoomPresetsMenu->addAction("200%", this, [this]() { zoomViewer(5); });
    m_zoomPresetsMenu->addAction("400%", this, [this]() { zoomViewer(6); });
    m_zoomPresetsMenu->addAction("800% (像素网格)", this, [this]() { zoomViewer(7); });
    viewMenu->addSeparator();
    viewMenu->addAction(m_actFullscreen);
    m_actSlideshow = new QAction("幻灯片放映(&S) (S)", this);
    m_actSlideshow->setObjectName("slideshowAction");
    m_actSlideshow->setCheckable(true);
    viewMenu->addAction(m_actSlideshow);
    viewMenu->addSeparator();
    m_actColorManagement = new QAction(tr("色彩管理 (CMS)"), this);
    m_actColorManagement->setObjectName("colorManagementAction");
    m_actColorManagement->setCheckable(true);
    m_actColorManagement->setChecked(DisplayColorContextProvider::isColorManagementEnabled());
    m_actColorManagement->setToolTip(
        tr("启用时根据显示器 ICC 转换颜色；禁用时使用原生 RGB/sRGB（与 FastStone 一致）"));
    viewMenu->addAction(m_actColorManagement);
    connect(m_actColorManagement, &QAction::toggled, this,
            [this](bool on)
            {
                DisplayColorContextProvider::setColorManagementEnabled(on);
                if (m_imageViewer)
                    m_imageViewer->setDisplayColorContext(
                        DisplayColorContextProvider::forWindow(m_imageViewer->windowHandle()));
                if (m_compareView)
                    m_compareView->setDisplayColorContext(
                        DisplayColorContextProvider::forWindow(m_compareView->windowHandle()));
            });
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
    auto *actGuide = new QAction("使用说明(&G)", this);
    connect(actGuide, &QAction::triggered, this, &MainWindow::showUserGuide);
    helpMenu->addAction(actGuide);
    auto *actShortcuts = new QAction("键盘快捷键(&K)", this);
    actShortcuts->setShortcut(QKeySequence(Qt::Key_F1));
    connect(actShortcuts, &QAction::triggered, this, &MainWindow::showShortcutsHelp);
    helpMenu->addAction(actShortcuts);
    m_actAbout = new QAction("关于(&A)", this);
    helpMenu->addAction(m_actAbout);
}
