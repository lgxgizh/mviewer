#include "mainwindow.h"

#include "application/OpenDirectoryUseCase.h"
#include "appstate.h"
#include "core/EventBus.h"
#include "core/RatingStore.h"
#include "core/SettingsIO.h"
#include "core/SidecarStore.h"
#include "core/analysis/ReportHtml.h"
#include "core/analyzer/Analyzer.h"
#include "core/cache/CacheManager.h"
#include "core/command/CallbackCommand.h"
#include "core/command/CompareCommand.h"
#include "core/command/DeleteCommand.h"
#include "core/command/OpenDirectoryCommand.h"
#include "core/command/RenameCommand.h"
#include "core/command/ToggleHistogramCommand.h"
// A-10: CommandStack is included via mainwindow.h
#include "core/export/ExportManager.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/RawMetadata.h"
#include "core/perf/MemoryTracker.h"
#include "core/project/ProjectSerializer.h"
#include "core/workspace/WorkspaceSerializer.h"

#include "analysispanel.h"
#include "analyzermodel.h"
#include "batchdialog.h"
#include "breadcrumbbar.h"
#include "compareworkspace.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/compare/Histogram.h"
#include "core/render/Viewport.h"
#include "directorymodel.h"
#include "directorytree.h"
#include "exportcommand.h"
#include "exportdialog.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "metadataoverlay.h"
#include "metadatapanel.h"
#include "pluginsettings.h"
#include "previewpanel.h"
#include "searchpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "workspacemodel.h"

#include <QApplication>
#include <QPointer>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <thread>
#include <QVBoxLayout>
#include "core/update/UpdateChecker.h"
#include <QWidget>

#include <algorithm>
#include <optional>

// M15 P0#1: forward declaration so openCompare() can restore a persisted
// session before the helper is defined later in this file.
static std::optional<mviewer::domain::CompareSession> decodeCompareSession(const std::string &json);

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    // M19: UI models — single source of truth for Current / Selection /
    // Directory / ImageList / Workspace / Analyzer. Every panel reacts to these
    // instead of tracking its own copy of the same state.
    m_selection = new SelectionModel(this);
    m_directory = new DirectoryModel(this);
    m_imageList = new ImageListModel(this);
    m_workspace = new WorkspaceModel(this);
    m_analyzer = new AnalyzerModel(this);

    // P0: load persisted cross-session state + recent-folders LRU before UI.
    m_appState = AppState::load();
    m_directory->setFavorites(m_appState.favorites);
    m_directory->setRecentFolders(m_appState.recentFolders);
    m_workspace->setAnalysisVisible(m_appState.analysisVisible);
    m_workspace->setAnalysisPage(m_appState.analysisPage);
    const QString recentPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recent.json";
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

    // M13.5: restore persisted window geometry/layout (QSettings, independent of workspace).
    {
        QSettings settings;
        if (settings.contains("geometry"))
        {
            restoreGeometry(settings.value("geometry").toByteArray());
            // If the restored window is entirely off-screen (e.g. the second
            // monitor was disconnected), re-center it on the primary screen.
            const QRect wr = frameGeometry();
            bool onAnyScreen = false;
            for (QScreen *scr : QGuiApplication::screens())
            {
                if (scr->availableGeometry().intersects(wr))
                {
                    onAnyScreen = true;
                    break;
                }
            }
            if (!onAnyScreen)
            {
                const QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
                move(ag.center() - QPoint(width() / 2, height() / 2));
            }
        }
        if (settings.contains("windowState"))
            restoreState(settings.value("windowState").toByteArray());
        // P1-7: closeEvent() already persists the splitter layout and the
        // thumbnail view mode, but they were never restored on launch — recover
        // them here so the panel widths and list style survive a restart exactly.
        if (m_mainSplitter && settings.contains("splitterState"))
            m_mainSplitter->restoreState(settings.value("splitterState").toByteArray());
        // A-6.4: restore left-sidebar width independently of the full splitter
        // state so a narrow/wide nav preference survives analysis/search toggles.
        if (m_mainSplitter && settings.contains("navSidebarWidth"))
        {
            const int navW = settings.value("navSidebarWidth").toInt();
            if (navW > 40)
            {
                QList<int> sizes = m_mainSplitter->sizes();
                if (!sizes.isEmpty())
                {
                    const int delta = navW - sizes[0];
                    sizes[0] = navW;
                    if (sizes.size() > 1)
                        sizes[1] = qMax(100, sizes[1] - delta);
                    m_mainSplitter->setSizes(sizes);
                }
            }
        }
        // A-6.4: restore vertical proportions inside the left sidebar.
        if (m_leftSplitter && settings.contains("leftSplitterState"))
            m_leftSplitter->restoreState(settings.value("leftSplitterState").toByteArray());
        if (m_thumbnailPanel && settings.contains("thumbViewMode"))
            m_thumbnailPanel->setViewMode(
                static_cast<ThumbnailPanel::ViewMode>(settings.value("thumbViewMode").toInt()));
        // Restore the last-used sort mode (Name/Date/Size/Resolution).
        if (m_sortCombo && settings.contains("thumbSortMode"))
        {
            const int sm = settings.value("thumbSortMode").toInt();
            for (int i = 0; i < m_sortCombo->count(); ++i)
                if (m_sortCombo->itemData(i).toInt() == sm)
                {
                    m_sortCombo->setCurrentIndex(i);
                    break;
                }
        }
    }

    // P0: restore last folder + image + scroll position (deferred to event loop).
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    rebuildRecentFilesMenu();
    restoreLastSession();

    // M14-1: open the file passed on the command line (deferred to event loop).
    if (!m_openOnLaunch.isEmpty())
        QMetaObject::invokeMethod(
            this, [this]() { onImageOpen(m_openOnLaunch); }, Qt::QueuedConnection);

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
    restoreSessionRecovery();
    // M17: if a previous run crashed, surface a crash-report prompt on next launch.
    maybeShowCrashReport();
    // M17: quiet background update check shortly after launch (only notifies on a new version).
    QTimer::singleShot(8000, this, [this]() { checkForUpdates(true); });
}

MainWindow::~MainWindow() = default;

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
    connect(actCheckUpdate, &QAction::triggered, this,
            [this]() { checkForUpdates(false); });
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
    sortCombo->addItem("相机", ThumbnailPanel::SortCamera);  // P0 #①
    sortCombo->addItem("镜头", ThumbnailPanel::SortLens);    // P0 #①
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

void MainWindow::setupCommands()
{
    auto &reg = CommandRegistry::instance();
    reg.registerCommand(
        std::make_unique<OpenDirectoryCommand>([this]() { m_actOpenDir->trigger(); }));
    reg.registerCommand(std::make_unique<CompareCommand>([this]() { openCompare(); }));
    reg.registerCommand(
        std::make_unique<RenameCommand>([this]() { m_thumbnailPanel->renameSelected(); }));
    reg.registerCommand(
        std::make_unique<DeleteCommand>([this]() { m_thumbnailPanel->moveToTrashSelected(); }));
    reg.registerCommand(
        std::make_unique<ToggleHistogramCommand>([this]() { m_actToggleAnalysis->trigger(); }));
    reg.registerCommand(std::make_unique<ExportCommand>(this));

    // M9 keyboard shortcuts (per product review P2.2): Left/Right navigate,
    // Space quick-preview current image, F toggles fullscreen. These delegate
    // to existing MainWindow handlers via CallbackCommand.
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "navigate_prev", "上一张 (Left)", [this]() { navigate(-1); },
        std::vector<CommandShortcut>{{Qt::Key_Left, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "navigate_next", "下一张 (Right)", [this]() { navigate(1); },
        std::vector<CommandShortcut>{{Qt::Key_Right, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "quick_preview", "在查看器中打开 (Enter)",
        [this]()
        {
            if (!currentImagePath().isEmpty())
                onImageOpen(currentImagePath());
        },
        std::vector<CommandShortcut>{{Qt::Key_Return, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "fullscreen", "全屏 (F)", [this]() { toggleFullscreen(); },
        std::vector<CommandShortcut>{{Qt::Key_F, 0}}));

    // M18: file-management shortcuts for the selected gallery items.
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_rename", "重命名 (F2)", [this]() { m_thumbnailPanel->renameSelected(); },
        std::vector<CommandShortcut>{{Qt::Key_F2, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_delete", "删除到回收站 (Delete)",
        [this]() { m_thumbnailPanel->moveToTrashSelected(); },
        std::vector<CommandShortcut>{{Qt::Key_Delete, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_copy", "复制到... (Ctrl+C)", [this]() { m_thumbnailPanel->copySelectedTo(); },
        std::vector<CommandShortcut>{{Qt::Key_C, Qt::ControlModifier}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_move", "移动到... (Ctrl+M)", [this]() { m_thumbnailPanel->moveSelectedTo(); },
        std::vector<CommandShortcut>{{Qt::Key_M, Qt::ControlModifier}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_reveal", "在资源管理器中显示 (Ctrl+E)",
        [this]() { m_thumbnailPanel->revealSelected(); },
        std::vector<CommandShortcut>{{Qt::Key_E, Qt::ControlModifier}}));
    // P0: Ctrl+F focuses the directory-tree filter for quick folder search.
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "dir_filter", "搜索目录 (Ctrl+F)",
        [this]()
        {
            if (m_directoryTree->filterEdit())
            {
                m_directoryTree->filterEdit()->setFocus();
                m_directoryTree->filterEdit()->selectAll();
            }
        },
        std::vector<CommandShortcut>{{Qt::Key_F, Qt::ControlModifier}}));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // P0-1: F5 refreshes the directory tree and the gallery from disk.
    if (event->key() == Qt::Key_F5 && !mod)
    {
        m_directoryTree->refresh();
        m_thumbnailPanel->refresh();
        m_imageList->markDirty();
        scheduleReindex();
        event->accept();
        return;
    }
    // P0-3 / A-5: ESC dismisses the metadata overlay AND the floating panel
    // (keeps the image area maximal for browsing).
    if (event->key() == Qt::Key_Escape && !mod)
    {
        bool dismissed = false;
        if (m_metadataOverlay && m_metadataOverlay->isVisible())
        {
            m_metadataOverlay->hide();
            dismissed = true;
        }
        if (m_metadataPanel && m_metadataPanel->isVisible())
        {
            m_metadataPanel->hide();
            dismissed = true;
        }
        if (dismissed)
        {
            if (m_actToggleMetadata)
                m_actToggleMetadata->setChecked(false);
            event->accept();
            return;
        }
    }
    // ESC exits fullscreen when the main window itself is fullscreen.
    if (event->key() == Qt::Key_Escape && !mod && isFullScreen())
    {
        showNormal();
        event->accept();
        return;
    }
    // P1-8: F1 shows the keyboard-shortcut cheat sheet.
    if (event->key() == Qt::Key_F1 && !mod)
    {
        showShortcutsHelp();
        event->accept();
        return;
    }
    // P1-8: Home/End jump to the first/last image; PageUp/PageDown jump a page.
    if (!mod && (event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
                 event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown))
    {
        navigatePage(event->key());
        event->accept();
        return;
    }
    // P3 tail: Ctrl+Shift+1..6 set a color label; Ctrl+Shift+0 clears it;
    // Ctrl+Shift+P toggles pick; Ctrl+Shift+X toggles reject.
    // Alt+0..6 sets color labels (moved from Ctrl+Shift+0..6 to free those for
    // star ratings, which in turn were moved from Ctrl+0..5 to avoid colliding
    // with Ctrl+1..6 view-mode shortcuts).
    if ((mod & Qt::AltModifier) && !event->isAutoRepeat())
    {
        if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_6)
        {
            setCurrentColorLabel(event->key() - Qt::Key_0);
            event->accept();
            return;
        }
    }
    if ((mod & Qt::ControlModifier) && (mod & Qt::ShiftModifier))
    {
        if (event->key() == Qt::Key_P)
        {
            toggleCurrentPick();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_X)
        {
            toggleCurrentReject();
            event->accept();
            return;
        }
    }
    // P1: Ctrl+Shift+0..5 rate the current image; Ctrl+Shift+0 clears.
    // (Was Ctrl+0..5, which collided with the Ctrl+1..6 view-mode shortcuts —
    //  Ctrl+1..5 could never reach view-mode switching.)
    if ((mod & Qt::ControlModifier) && (mod & Qt::ShiftModifier) && event->key() >= Qt::Key_0 &&
        event->key() <= Qt::Key_5)
    {
        rateCurrentImage(event->key() - Qt::Key_0);
        event->accept();
        return;
    }
    // P0-3 / P1-4 / M19: 'I' or 'M' toggles the metadata overlay (I is the
    // product-facing shortcut; M kept for muscle memory).
    if ((event->key() == Qt::Key_I || event->key() == Qt::Key_M) && !mod)
    {
        toggleMetadataOverlay();
        event->accept();
        return;
    }
    // P0-2 / P1-4: view-mode shortcuts.
    if (event->key() == Qt::Key_G && !mod)
    {
        m_thumbnailPanel->setViewMode(ThumbnailPanel::Thumbnail);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_D && !mod)
    {
        m_thumbnailPanel->setViewMode(ThumbnailPanel::Details);
        event->accept();
        return;
    }
    if ((mod & Qt::ControlModifier) && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6)
    {
        static const ThumbnailPanel::ViewMode modes[] = {
            ThumbnailPanel::Thumbnail, ThumbnailPanel::List, ThumbnailPanel::Details,
            ThumbnailPanel::Filmstrip, ThumbnailPanel::SmallIcon, ThumbnailPanel::Compact};
        m_thumbnailPanel->setViewMode(modes[event->key() - Qt::Key_1]);
        event->accept();
        return;
    }
    // P1-4: 'H' toggles the analysis (histogram) panel.
    if (event->key() == Qt::Key_H && !mod)
    {
        if (m_actToggleAnalysis)
            m_actToggleAnalysis->trigger();
        event->accept();
        return;
    }
    // P1-4: Tab toggles side panels (left + analysis + search) for a clean view.
    if (event->key() == Qt::Key_Tab && !mod)
    {
        const bool visible = m_directoryTree->isVisible();
        m_directoryTree->setVisible(!visible);
        m_previewPanel->setVisible(!visible);
        m_analysisPanel->setVisible(!visible);
        m_searchPanel->setVisible(!visible);
        event->accept();
        return;
    }
    // P1-4: Ctrl+C copies the current image to clipboard; Ctrl+Shift+C copies its path.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        if ((mod & Qt::ShiftModifier))
        {
            if (!currentImagePath().isEmpty())
                QApplication::clipboard()->setText(currentImagePath());
        }
        else
        {
            copyCurrentImageToClipboard();
        }
        event->accept();
        return;
    }
    // Ctrl+V: paste an image from the clipboard (e.g. after a screenshot) and
    // view it directly — common screenshot-to-viewer workflow.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_V && !(mod & Qt::ShiftModifier))
    {
        const QClipboard *cb = QApplication::clipboard();
        const QMimeData *md = cb->mimeData();
        if (md && md->hasImage())
        {
            const QImage img = qvariant_cast<QImage>(md->imageData());
            if (!img.isNull())
            {
                // Persist to a temp file so ImageViewer can load it via its
                // normal async path (keeps decode/histogram consistent).
                const QString tmpDir =
                    QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                    "/mviewer-clip-paste";
                QDir().mkpath(tmpDir);
                const QString tmpPath = tmpDir + "/paste_" +
                                        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") +
                                        ".png";
                if (img.save(tmpPath, "PNG"))
                {
                    onImageOpen(tmpPath);
                    statusBar()->showMessage("已从剪贴板粘贴图片", 3000);
                }
                else
                    statusBar()->showMessage("无法保存剪贴板图片", 3000);
            }
            else
                statusBar()->showMessage("剪贴板中无图片数据", 3000);
        }
        else
            statusBar()->showMessage("剪贴板中无图片数据", 3000);
        event->accept();
        return;
    }
    // P0-4 / P1-4: Space triggers compare for the current + next image.
    if (event->key() == Qt::Key_Space && !mod)
    {
        openQuickCompare();
        event->accept();
        return;
    }
    // Compare mode on a plain 'C' — same style as G/D/H/M above. (A QAction
    // plain-key shortcut would shadow text entry in the search box.)
    if (event->key() == Qt::Key_C && !mod)
    {
        m_actCompare->trigger();
        event->accept();
        return;
    }
    // 'S' toggles the slideshow (same plain-key rationale as 'C').
    if (event->key() == Qt::Key_S && !mod)
    {
        toggleSlideshow();
        event->accept();
        return;
    }
    // Viewer zoom keys: plain +/-/0/1 (forwarded to the viewer when visible).
    if (!mod && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal))
    {
        zoomViewer(0);
        event->accept();
        return;
    }
    if (!mod && event->key() == Qt::Key_Minus)
    {
        zoomViewer(1);
        event->accept();
        return;
    }
    if (!mod && event->key() == Qt::Key_0)
    {
        zoomViewer(2);
        event->accept();
        return;
    }
    if (!mod && event->key() == Qt::Key_1)
    {
        zoomViewer(3);
        event->accept();
        return;
    }
    ICommand *cmd = CommandRegistry::instance().findByShortcut(
        event->key(), static_cast<int>(event->modifiers()));
    if (cmd)
    {
        cmd->execute();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
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

    std::vector<std::string> paths;
    std::vector<mviewer::domain::ImageMetadata> metas;
    std::vector<mviewer::core::RawMetadata> raws;

    for (const QString &p : m_imageList->paths())
    {
        const std::string sp = p.toStdString();
        paths.push_back(sp);

        auto meta = mviewer::core::MetadataReader::read(sp);
        metas.push_back(meta);

        auto raw = mviewer::core::parseRawMetadata(sp);
        raws.push_back(raw);
    }

    m_searchPanel->reindex(paths, metas, raws);
}

void MainWindow::onRatingFilterChanged(int)
{
    m_thumbnailPanel->setRatingFilter(m_ratingFilter->currentData().toInt());
}

void MainWindow::rateCurrentImage(int stars)
{
    if (currentImagePath().isEmpty())
        return;
    mviewer::core::RatingStore::instance().setRating(currentImagePath().toStdString(), stars);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath()); // refresh the rating widget
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toStdString());
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
    mviewer::core::RatingStore::instance().setColorLabel(currentImagePath().toStdString(), label);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toStdString());
    const QString name = QFileInfo(currentImagePath()).fileName();
    statusBar()->showMessage(label == 0 ? QString("已清除 %1 的色标").arg(name)
                                        : QString("已为 %1 设置色标 %2").arg(name).arg(label));
}

void MainWindow::toggleCurrentPick()
{
    if (currentImagePath().isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.picked(currentImagePath().toStdString());
    rs.setPicked(currentImagePath().toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toStdString());
    statusBar()->showMessage(
        v ? QString("已收藏 %1").arg(QFileInfo(currentImagePath()).fileName())
          : QString("已取消收藏 %1").arg(QFileInfo(currentImagePath()).fileName()));
}

void MainWindow::toggleCurrentReject()
{
    if (currentImagePath().isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.rejected(currentImagePath().toStdString());
    rs.setRejected(currentImagePath().toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(currentImagePath());
    mviewer::core::SidecarStore::instance().writeSidecar(currentImagePath().toStdString());
    statusBar()->showMessage(
        v ? QString("已拒绝 %1").arg(QFileInfo(currentImagePath()).fileName())
          : QString("已取消拒绝 %1").arg(QFileInfo(currentImagePath()).fileName()));
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
        m_imageViewer->setImage(path); // async; imageReady() feeds AnalysisPanel

    // --- "open" extras (only meaningful for an explicit open, not selection) ---
    pushHistory(path); // P0: in-session browse history
    // P0-1: cross-session image history.
    m_appState.addHistory(path);
    // M14-1: track in recent-files LRU + refresh menu.
    m_recentFiles.add(path.toStdString());
    rebuildRecentFilesMenu();
    // M12.2 / M19: restore saved analysis from AnalyzerModel if present.
    const QString savedAnalysis = m_analyzer->resultText(path);
    if (!savedAnalysis.isEmpty())
        m_analysisPanel->setRegionStats(savedAnalysis);
    if (wasHidden)
        m_imageViewer->show();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
}

void MainWindow::onCurrentImageChanged(const QString &path)
{
    // P0-2 / M19: the ONE place that fans the current-image change out to every
    // view. SelectionModel already holds the path — do not re-set it here.
    if (path.isEmpty())
        return;

    const QFileInfo fi(path);
    m_previewPanel->setImage(path); // async decode (off UI thread)
    // Only decode into the viewer when it is actually on screen — avoids a
    // second decode per thumbnail while browsing with the viewer closed.
    if (!m_imageViewer->isHidden())
        m_imageViewer->setImage(path);

    // Metadata: the overlay follows its toggle; the (usually hidden) tool panel
    // is refreshed only when visible so rapid browsing stays cheap.
    if (m_metadataOverlay)
    {
        m_metadataOverlay->setImage(path);
        if (m_actToggleMetadata && m_actToggleMetadata->isChecked())
            m_metadataOverlay->showForImage(path);
    }
    if (m_metadataPanel && m_metadataPanel->isVisible())
        m_metadataPanel->setImage(path);

    // Keep the thumbnail-grid highlight in lock-step (no-op if already current).
    m_thumbnailPanel->selectPath(path);

    // P0: Auto-locate the directory tree to the image's parent folder (navigateTo
    // expands ancestors & scrolls, does NOT change image selection → no loop).
    const QString dir = fi.absolutePath();
    if (m_directoryTree && !dir.isEmpty())
        m_directoryTree->navigateTo(dir);

    mviewer::core::RatingStore::instance().addRecent(path.toStdString()); // P3 recents

    // Window title + status bar identity follow the current image.
    setWindowTitle(QString("%1 - MViewer").arg(fi.fileName()));
    // Cheap header-only read (MetadataReader decodes at 1x1) for dimensions;
    // file size comes straight from the filesystem entry.
    const auto meta = mviewer::core::MetadataReader::read(path.toStdString());
    if (meta.width > 0 && meta.height > 0)
        m_lblImage->setText(
            QString("%1x%2 · %3").arg(meta.width).arg(meta.height).arg(formatBytes(fi.size())));
    else
        m_lblImage->setText(formatBytes(fi.size()));
    statusBar()->showMessage(QString("当前: %1").arg(fi.fileName()));
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

    // Update the path input bar to reflect the new directory.
    if (m_pathEdit)
        m_pathEdit->setText(QDir::toNativeSeparators(dir));

    // Navigate the tree with emitSignal=true so the directoryChanged signal
    // fires, triggering the full update chain (breadcrumb, thumbnails, recent
    // folders, status bar, reindex, etc.) — just like clicking a tree node.
    m_directoryTree->navigateTo(dir, true);

    // P0: push directory-level history for back/forward navigation.
    pushDirHistory(dir);

    // Import sidecar metadata for the new directory.
    mviewer::core::SidecarStore::instance().importDirectory(dir.toStdString());
}

void MainWindow::openCompare(const QStringList &images, const QString &sessionJson)
{
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
    if (imgs.isEmpty())
        return;

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("比较模式 - MViewer");
    dlg->resize(1000, 700);

    auto *layout = new QVBoxLayout(dlg);
    m_compareView = new CompareWorkspace(dlg);
    layout->addWidget(m_compareView);
    // P0: Inject SelectionModel so CompareWorkspace writes focus back to global SSOT.
    m_compareView->setSelectionModel(m_selection);
    m_compareView->setImages(imgs);
    // A-4.5 / M19: continuous compare — seed the pool from ImageListModel.
    ensureImageList();
    if (m_imageList && !m_imageList->isEmpty())
        m_compareView->setImagePool(m_imageList->paths());
    else if (imgs.size() > 2)
        m_compareView->setImagePool(imgs);
    // M19: WorkspaceModel tracks the live compare set.
    if (m_workspace)
        m_workspace->setComparedImages(imgs);
    connect(m_compareView, &CompareWorkspace::pixelInfo, this,
            [this](const QString &text) { statusBar()->showMessage(text); });
    // P1 #④: Compare → Analyze workflow (Analyze button in Compare toolbar).
    connect(m_compareView, &CompareWorkspace::analyzeCurrent, this,
            [this]()
            {
                if (m_compareView)
                {
                    const QString path = m_compareView->focusImagePath();
                    if (!path.isEmpty())
                    {
                        m_selection->setCurrentImage(path);
                        m_imageViewer->setImage(path);
                        m_analysisPanel->setImage(QImage(path), path);
                    }
                }
                if (m_analysisPanel && !m_analysisPanel->isVisible())
                    m_analysisPanel->show();
            });
    // P1 #④: Compare → Export Report workflow (Export button in Compare toolbar).
    connect(m_compareView, &CompareWorkspace::exportReportRequested, this,
            [this]() { exportReport(); });

    connect(dlg, &QDialog::destroyed, this,
            [this]()
            {
                m_compareView = nullptr;
                disconnect(m_compareDestroyConnection);
            });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();

    // Load images *after* the dialog has been shown AND the event loop has
    // processed the layout pass, so that cell widgets have valid geometry when
    // fitAll() computes the shared zoom scale. Without this delay, cell
    // size() returns (0,0), fitAll() skips every cell, and the shared scale
    // stays at 1.0 — making large images render off-screen and the compare
    // view appears blank.
    const QStringList imgsFinal = imgs;
    const QString sessionFinal = sessionJson;
    QTimer::singleShot(0, this,
                       [this, imgsFinal, sessionFinal]()
                       {
                           if (!m_compareView)
                               return;
                           m_compareView->setImages(imgsFinal);

                           // M15 P0#1: restore persisted compare session after images
                           // are loaded.
                           if (!sessionFinal.isEmpty())
                           {
                               const auto session =
                                   decodeCompareSession(sessionFinal.toStdString());
                               if (session)
                                   m_compareView->applySession(*session);
                           }
                       });
}

void MainWindow::navigate(int delta)
{
    if (currentDir().isEmpty() || currentImagePath().isEmpty())
        return;

    // M19: ImageListModel is the SSOT for the directory listing.
    ensureImageList();
    const QStringList list = m_imageList->paths();
    if (list.isEmpty())
        return;

    int idx = list.indexOf(currentImagePath());
    if (idx < 0)
        idx = 0;
    // Wrap around at both ends (FastStone/ImageGlass parity; also keeps the
    // slideshow advancing past the last image).
    const int next = (idx + delta + list.size()) % list.size();

    const QString path = list.at(next);
    // P0-2: single source of truth. onCurrentImageChanged() now also keeps the
    // thumbnail-grid highlight in sync with keyboard navigation — previously the
    // grid selection lagged behind the viewer when using the arrow keys.
    m_selection->setCurrentImage(path);
}

void MainWindow::navigatePage(int key)
{
    if (currentDir().isEmpty())
        return;
    ensureImageList();
    const QStringList list = m_imageList->paths();
    if (list.isEmpty())
        return;

    int idx = list.indexOf(currentImagePath());
    if (idx < 0)
        idx = 0;
    constexpr int kPage = 10; // images per PageUp/PageDown step
    int target = idx;
    switch (key)
    {
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = list.size() - 1;
        break;
    case Qt::Key_PageUp:
        target = qMax(0, idx - kPage);
        break;
    case Qt::Key_PageDown:
        target = qMin(list.size() - 1, idx + kPage);
        break;
    default:
        return;
    }
    if (target != idx || currentImagePath().isEmpty())
        m_selection->setCurrentImage(list.at(target));
}

void MainWindow::showShortcutsHelp()
{
    // P1-8: a single, authoritative cheat sheet so users never have to guess.
    const QString html = QStringLiteral(
        "<style>td{padding:2px 14px 2px 0;} th{text-align:left;padding-top:8px;}"
        "kbd{background:#333;color:#fff;border-radius:3px;padding:1px 5px;}</style>"
        "<table>"
        "<tr><th colspan='2'>文件</th></tr>"
        "<tr><td><kbd>Ctrl+O</kbd> / <kbd>Ctrl+Shift+O</kbd></td><td>打开目录 / 打开文件</td></tr>"
        "<tr><td><kbd>Ctrl+V</kbd></td><td>从剪贴板粘贴图片（截图后直接查看）</td></tr>"
        "<tr><td><kbd>Ctrl+D</kbd></td><td>收藏当前目录</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+F</kbd></td><td>全局搜索</td></tr>"
        "<tr><td><kbd>Ctrl+Q</kbd></td><td>退出</td></tr>"
        "<tr><th colspan='2'>浏览</th></tr>"
        "<tr><td><kbd>←</kbd> / <kbd>→</kbd> / 鼠标侧键</td><td>上一张 / 下一张（循环）</td></tr>"
        "<tr><td><kbd>Alt+←</kbd> / <kbd>Alt+→</kbd></td><td>历史导航：上一步 / 下一步</td></tr>"
        "<tr><td><kbd>Enter</kbd></td><td>在查看器中打开选中图片</td></tr>"
        "<tr><td><kbd>Home</kbd> / <kbd>End</kbd></td><td>第一张 / "
        "最后一张（查看器中同样有效）</td></tr>"
        "<tr><td><kbd>PageUp</kbd> / <kbd>PageDown</kbd></td><td>上翻 / 下翻一页（10 "
        "张，查看器中同样有效）</td></tr>"
        "<tr><td><kbd>F5</kbd></td><td>刷新目录树与画廊</td></tr>"
        "<tr><td><kbd>Ctrl+滚轮</kbd></td><td>调整缩略图大小</td></tr>"
        "<tr><td><kbd>Tab</kbd></td><td>显示 / 隐藏侧边面板</td></tr>"
        "<tr><th colspan='2'>缩放（查看器）</th></tr>"
        "<tr><td><kbd>+</kbd> / <kbd>-</kbd>（或 <kbd>Ctrl++</kbd> / <kbd>Ctrl+-</kbd>）</td><td>"
        "放大 / 缩小</td></tr>"
        "<tr><td><kbd>0</kbd> / <kbd>1</kbd></td><td>适应窗口 / 实际大小</td></tr>"
        "<tr><td>双击</td><td>适应窗口 ↔ 100% 切换</td></tr>"
        "<tr><td><kbd>F</kbd> / <kbd>F11</kbd></td><td>全屏切换</td></tr>"
        "<tr><td><kbd>S</kbd></td><td>幻灯片放映（3 秒/张，循环）</td></tr>"
        "<tr><td><kbd>ESC</kbd></td><td>退出全屏 / 关闭查看器 / 停止放映 / 关闭信息浮层</td></tr>"
        "<tr><th colspan='2'>视图模式</th></tr>"
        "<tr><td><kbd>G</kbd></td><td>缩略图视图</td></tr>"
        "<tr><td><kbd>D</kbd></td><td>详情视图</td></tr>"
        "<tr><td><kbd>Ctrl+1</kbd>…<kbd>Ctrl+4</kbd></td><td>缩略图 / 列表 / 详情 / 胶片条</td></tr>"
        "<tr><td><kbd>Ctrl+5</kbd> / <kbd>Ctrl+6</kbd></td><td>小图标 / 紧凑</td></tr>"
        "<tr><th colspan='2'>比较（比较窗口内）</th></tr>"
        "<tr><td><kbd>C</kbd></td><td>打开比较模式</td></tr>"
        "<tr><td><kbd>Space</kbd></td><td>按住 Blink / 主窗口快速比较</td></tr>"
        "<tr><td><kbd>B</kbd> / <kbd>S</kbd> / <kbd>W</kbd> / <kbd>O</kbd></td>"
        "<td>Blink / Split / Swipe / Overlay</td></tr>"
        "<tr><td><kbd>H</kbd></td><td>Diff 高亮</td></tr>"
        "<tr><td><kbd>Z</kbd> / <kbd>D</kbd></td><td>同步缩放 / 同步拖动</td></tr>"
        "<tr><td><kbd>C</kbd> / <kbd>L</kbd> / <kbd>I</kbd></td><td>准星 / 像素连线 / "
        "侧栏</td></tr>"
        "<tr><td><kbd>1</kbd>~<kbd>8</kbd></td><td>N 联布局预设（比较 N 张）</td></tr>"
        "<tr><td><kbd>PgUp</kbd>/<kbd>PgDn</kbd> / <kbd>←</kbd>/<kbd>→</kbd></td>"
        "<td>连续导航（保留模式）</td></tr>"
        "<tr><td><kbd>F</kbd> / <kbd>X</kbd> / <kbd>?</kbd></td><td>Fit / 交换窗格 / 帮助</td></tr>"
        "<tr><td><kbd>ESC</kbd></td><td>关闭比较窗口</td></tr>"
        "<tr><th colspan='2'>分析 / 信息</th></tr>"
        "<tr><td><kbd>H</kbd></td><td>直方图 / 分析面板</td></tr>"
        "<tr><td><kbd>I</kbd> / <kbd>M</kbd></td><td>图片信息浮层（ESC 关闭；浮层内 Ctrl+C "
        "复制全部元数据）</td></tr>"
        "<tr><th colspan='2'>评分 / 标签</th></tr>"
        "<tr><td><kbd>Ctrl+Shift+0</kbd>…<kbd>Ctrl+Shift+5</kbd></td><td>评分（0 = 清除）</td></tr>"
        "<tr><td><kbd>Alt+0</kbd>…<kbd>Alt+6</kbd></td><td>颜色标签（0 = "
        "清除）</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+P</kbd> / <kbd>Ctrl+Shift+X</kbd></td><td>标记选中 / "
        "拒绝</td></tr>"
        "<tr><th colspan='2'>剪贴板</th></tr>"
        "<tr><td><kbd>Ctrl+C</kbd> / <kbd>Ctrl+Shift+C</kbd></td><td>复制图片 / 复制路径</td></tr>"
        "<tr><th colspan='2'>文件操作</th></tr>"
        "<tr><td><kbd>F2</kbd></td><td>重命名选中图片</td></tr>"
        "<tr><td><kbd>Delete</kbd></td><td>删除到回收站</td></tr>"
        "<tr><td><kbd>Ctrl+M</kbd></td><td>移动到...</td></tr>"
        "<tr><td><kbd>Ctrl+E</kbd></td><td>在资源管理器中显示</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+B</kbd></td><td>批量处理</td></tr>"
        "</table>");

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("键盘快捷键"));
    dlg.resize(480, 560);
    auto *lay = new QVBoxLayout(&dlg);
    auto *browser = new QTextBrowser(&dlg);
    browser->setHtml(html);
    browser->setOpenExternalLinks(false);
    lay->addWidget(browser);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

void MainWindow::onBreadcrumbPath(const QString &path)
{
    // M15 Product Shell P0: navigate the directory tree to the breadcrumb path.
    if (!path.isEmpty())
    {
        m_directoryTree->navigateTo(path, true);
        pushDirHistory(path);
    }
}

void MainWindow::exportReport()
{
    // M14-4: collect current view data and build an HTML report.
    mviewer::core::ReportContext ctx;
    ctx.title = "MViewer Analysis Report";

    if (currentImagePath().isEmpty())
    {
        QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
        return;
    }
    ctx.imagePath = currentImagePath().toStdString();

    // Grab the histogram pixmap from the analysis panel (if rendered).
    if (m_analysisPanel)
    {
        QPixmap hist = m_analysisPanel->histogramPixmap();
        if (!hist.isNull())
        {
            QByteArray buf;
            QBuffer stream(&buf);
            hist.save(&stream, "PNG");
            ctx.histogramPng = buf.toBase64().toStdString();
        }
    }

    // Compare data (if a compare session is active).
    if (m_compareView)
    {
        const mviewer::domain::CompareSession sess = m_compareView->compareSession();
        // Only meaningful if 2+ images.
        const int n = m_compareView->engine().imageCount();
        if (n >= 2)
        {
            const ImageFrame *a = m_compareView->engine().imageAt(0);
            const ImageFrame *b = m_compareView->engine().imageAt(1);
            if (a && b)
            {
                ctx.compare = mviewer::core::buildCompareReport(*a, *b);
                ctx.hasCompare = true;
                ImageData diffImg = mviewer::core::compareDiffImage(*a, *b);
                if (!diffImg.isNull())
                {
                    // Convert to PNG base64 (via Qt).
                    QImage q = mvcore::toQImage(diffImg);
                    QByteArray buf;
                    QBuffer stream(&buf);
                    q.save(&stream, "PNG");
                    ctx.compareDiffPng = buf.toBase64().toStdString();
                }
            }
        }
    }

    const std::string html = mviewer::core::buildReportHtml(ctx);
    if (html.empty())
    {
        QMessageBox::warning(this, tr("导出报告"), tr("报告内容为空。"));
        return;
    }

    const QString out = QFileDialog::getSaveFileName(this, tr("导出报告"), QString(),
                                                     tr("HTML 文件 (*.html);;Markdown 文件 (*.md);;"
                                                        "JSON 文件 (*.json)"));
    if (out.isEmpty())
        return;
    const QFileInfo fi(out);
    const QString suffix = fi.suffix().toLower();

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("错误"), tr("无法写入：%1").arg(out));
        return;
    }
    if (suffix == "json")
    {
        // JSON: emit the compare report only (structured data).
        std::string json = "{\"error\":\"no compare data\"}";
        if (ctx.hasCompare)
            json = ctx.compare.toJson();
        f.write(QByteArray::fromStdString(json));
    }
    else if (suffix == "md")
    {
        // P1 #⑥: Markdown export — simple structured report.
        QString md;
        md += QString("# %1\n\n").arg(QString::fromStdString(ctx.title));
        md += QString("**图像**: `%1`\n\n").arg(QString::fromStdString(ctx.imagePath));
        if (!ctx.histogramPng.empty())
            md += QString("![直方图](data:image/png;base64,%1)\n\n")
                      .arg(QString::fromStdString(ctx.histogramPng));
        if (ctx.hasCompare)
        {
            md += "## 对比报告\n\n";
            md += QString::fromStdString(ctx.compare.toJson()); // structured diff as JSON block
            if (!ctx.compareDiffPng.empty())
                md += QString("\n\n![Diff](data:image/png;base64,%1)\n")
                          .arg(QString::fromStdString(ctx.compareDiffPng));
        }
        f.write(md.toUtf8());
    }
    else
    {
        f.write(QByteArray::fromStdString(html));
    }
    f.close();
    QMessageBox::information(this, tr("导出报告"), tr("已导出：%1").arg(out));
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
    // A-3.4: enable Compare when ≥2 selected; Export/Batch when ≥1 path available.
    const int n = m_selection ? m_selection->selection().size() : 0;
    const bool hasDir = (m_imageList && !m_imageList->isEmpty()) ||
                        (m_thumbnailPanel && !m_thumbnailPanel->pathList().isEmpty());
    if (m_actCompare)
        m_actCompare->setEnabled(n >= 2 || hasDir);
    if (m_actExportImages)
        m_actExportImages->setEnabled(n >= 1 || hasDir);
    if (m_actBatch)
        m_actBatch->setEnabled(n >= 1 || hasDir);
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
    if (!m_imageList->isDirty() && m_imageList->directory() == dir && !m_imageList->isEmpty())
        return;
    QStringList paths;
    for (const auto &p : OpenDirectoryUseCase::execute(dir.toStdString()).imagePaths)
        paths.append(QString::fromStdString(p));
    m_imageList->setPaths(paths, dir);
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

void MainWindow::exportImages()
{
    // A-3 / M17: SelectionModel → gallery selection → filtered (rating/flag) set
    // → full directory. Prefer the filtered visible set over the unfiltered
    // directory so "export what I see" matches the rating/flag filters.
    QStringList paths = resolveSelectedPaths(true);
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->visiblePaths(); // post-filter set
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->pathList();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先打开一个图片目录。"));
        return;
    }

    ExportDialog dlg(this);
    dlg.setSources(paths);
    // Surface how many images will be exported (helps when filters are active).
    dlg.setWindowTitle(tr("导出图片 — %1 张").arg(paths.size()));
    dlg.exec();
}

void MainWindow::saveWorkspace()
{
    if (currentDir().isEmpty())
    {
        QMessageBox::information(this, "保存工作区", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存工作区", currentDir() + "/workspace.mvws", "MViewer 工作区 (*.mvws)");
    if (filePath.isEmpty())
        return;

    // Build the domain model from the real directory (recursive, no pixel
    // decode) using the existing, tested ImageRepository::loadWorkspace.
    mviewer::domain::Workspace ws =
        ImageRepository::instance().loadWorkspace(currentDir().toStdString());
    if (ws.empty())
    {
        QMessageBox::warning(this, "保存工作区", "当前目录没有可保存的图片。");
        return;
    }

    // M12.2 (G2-ext): persist every compared image's session context (ROI from
    // Compare + last analysis result) into the model before serializing, so
    // reopening restores the full compare session, not just the active image.
    // The compare ROI is synchronized across cells, so currentROI() is the same
    // region for all compared images; we still write it per-image into each
    // ImageMetadata so the .mvws carries each image's own ROI/analysis fields.
    mviewer::domain::Selection roi;
    QStringList compared;
    if (m_compareView)
    {
        roi = m_compareView->currentROI();
        compared = m_compareView->comparedImages();
        // M12.2 (review fix): persist the explicit compared-image list so a compare
        // session with neither ROI nor analysis still reopens correctly.
        for (const QString &cpath : compared)
            ws.comparedImages.push_back(cpath.toStdString());
        // M15: persist the full compare-session snapshot (sync mode, zoom/pan, ROI)
        // so reopening restores the entire compare view, not just the image list.
        if (m_compareView->compareSession().isValid())
            ws.compareSessionJson =
                mviewer::core::serializeCompareSession(m_compareView->compareSession());
    }
    for (const QString &cpath : compared)
    {
        const std::string key = cpath.toStdString();
        const std::string analysis = m_analyzer->resultText(cpath).toStdString();
        if (roi.isEmpty() && analysis.empty())
            continue;
        for (auto &folder : ws.folders)
        {
            for (auto &img : folder.imageSet.images)
            {
                if (img.filePath == key)
                {
                    img.roiX = roi.x;
                    img.roiY = roi.y;
                    img.roiW = roi.width;
                    img.roiH = roi.height;
                    img.analysis = analysis;
                    break;
                }
            }
        }
    }

    const std::string json = mviewer::core::serializeWorkspace(ws);
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(json.c_str()) < 0)
    {
        QMessageBox::critical(this, "保存工作区", "无法写入文件：" + filePath);
        return;
    }
    statusBar()->showMessage(QString("工作区已保存: %1 (%2 张图片, %3 个目录)")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(static_cast<int>(ws.imageCount()))
                                 .arg(static_cast<int>(ws.folderCount())));
}

void MainWindow::openWorkspace()
{
    const QString filePath =
        QFileDialog::getOpenFileName(this, "打开工作区", QString(), "MViewer 工作区 (*.mvws)");
    if (filePath.isEmpty())
        return;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "打开工作区", "无法读取文件：" + filePath);
        return;
    }
    const QByteArray data = f.readAll();
    const auto maybeWs =
        mviewer::core::deserializeWorkspace(std::string(data.constData(), data.size()));
    if (!maybeWs || maybeWs->empty())
    {
        QMessageBox::critical(this, "打开工作区", "工作区文件无效或为空。");
        return;
    }
    mviewer::domain::Workspace ws = std::move(*maybeWs);

    // Restore the browsing view: load the workspace root back into the gallery.
    // changeDirectory drives DirectoryModel + ImageListModel + tree + gallery.
    const QString root = QString::fromStdString(ws.rootPath);
    changeDirectory(root);
    if (m_workspace)
    {
        m_workspace->setRootPath(root);
        QStringList cmp;
        for (const auto &p : ws.comparedImages)
            cmp.append(QString::fromStdString(p));
        m_workspace->setComparedImages(cmp);
        m_workspace->setCompareSessionJson(QString::fromStdString(ws.compareSessionJson));
    }

    // M12.2 (review fix): restore the compare session from the explicit
    // comparedImages list written by saveWorkspace(). This is the exact set of
    // images that were open in Compare — independent of whether they had ROI or
    // analysis context — so a session with neither is no longer lost on reopen.
    // (Earlier G2-ext code inferred the set from ROI/analysis presence, which
    // dropped compare sessions with no ROI and no analysis.)
    QStringList comparePaths;
    comparePaths.reserve(static_cast<int>(ws.comparedImages.size()));
    for (const auto &p : ws.comparedImages)
        comparePaths.push_back(QString::fromStdString(p));

    // M15: rebuild the per-image analysis map from the saved model so the whole
    // compare session's analysis context is available on reload (each image's
    // own ROI/analysis is restored, not just the first). openCompare() below
    // creates m_compareView; we apply the per-image context after it loads.
    m_analyzer->clearAllResults();
    for (const auto &folder : ws.folders)
    {
        for (const auto &img : folder.imageSet.images)
        {
            if (!img.analysis.empty())
                m_analyzer->setResult(QString::fromStdString(img.filePath),
                                      QString::fromStdString(img.analysis));
        }
    }

    // M15: if a compare session was saved, auto-open the compare dialog (it may
    // not exist yet in a fresh launch) and load the exact image set. Previously
    // the session was silently dropped when m_compareView was still null.
    std::optional<mviewer::domain::CompareSession> restoredSession;
    bool haveSession = false;
    if (!ws.compareSessionJson.empty())
    {
        restoredSession = mviewer::core::deserializeCompareSession(ws.compareSessionJson);
        haveSession = restoredSession.has_value();
    }
    if (!comparePaths.isEmpty())
    {
        openCompare(comparePaths); // creates m_compareView + setImages + show
        // openCompare() shows the dialog; restore the saved transform snapshot.
        if (haveSession && m_compareView)
            m_compareView->applySession(*restoredSession);
    }

    // Pick the active (browsing) image: prefer the first image carrying session
    // context (ROI or analysis), else the first compared image, else the first
    // image in the workspace.
    std::string restoredPath;
    mviewer::domain::Selection restoredRoi;
    std::string restoredAnalysis;
    for (const auto &folder : ws.folders)
    {
        for (const auto &img : folder.imageSet.images)
        {
            if (restoredPath.empty() && (img.roiW > 0 || img.roiH > 0 || !img.analysis.empty()))
            {
                restoredRoi = {img.roiX, img.roiY, img.roiW, img.roiH};
                restoredAnalysis = img.analysis;
                restoredPath = img.filePath;
            }
        }
    }
    if (restoredPath.empty() && !comparePaths.isEmpty())
        restoredPath = comparePaths.first().toStdString();
    else if (restoredPath.empty() && ws.imageCount() > 0)
        restoredPath = ws.folders.front().imageSet.images.front().filePath;

    if (!restoredPath.empty())
    {
        m_selection->setCurrentImage(QString::fromStdString(restoredPath));
        if (m_imageViewer)
        {
            // Async decode; imageReady() feeds AnalysisPanel once the frame is
            // ready (no synchronous frame() read here — it isn't ready yet).
            m_imageViewer->setImage(currentImagePath());
            m_previewPanel->setImage(currentImagePath());
        }
        if (!restoredAnalysis.empty())
            m_analysisPanel->setRegionStats(QString::fromStdString(restoredAnalysis));
        if (!restoredRoi.isEmpty() && m_compareView)
            m_compareView->applyROI(restoredRoi);
    }

    statusBar()->showMessage(QString("工作区已打开: %1 (%2 张图片, %3 个目录)")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(static_cast<int>(ws.imageCount()))
                                 .arg(static_cast<int>(ws.folderCount())));
}

void MainWindow::saveProject()
{
    if (currentDir().isEmpty())
    {
        QMessageBox::information(this, "保存项目", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存项目", currentDir() + "/project.mvproj", "MViewer 项目 (*.mvproj)");
    if (filePath.isEmpty())
        return;

    // Build the workspace exactly like saveWorkspace (datasets + compared images
    // + compare-session snapshot + per-image ROI/analysis).
    mviewer::domain::Workspace ws =
        ImageRepository::instance().loadWorkspace(currentDir().toStdString());
    if (ws.empty())
    {
        QMessageBox::warning(this, "保存项目", "当前目录没有可保存的图片。");
        return;
    }

    mviewer::domain::Selection roi;
    QStringList compared;
    if (m_compareView)
    {
        roi = m_compareView->currentROI();
        compared = m_compareView->comparedImages();
        for (const QString &cpath : compared)
            ws.comparedImages.push_back(cpath.toStdString());
        if (m_compareView->compareSession().isValid())
            ws.compareSessionJson =
                mviewer::core::serializeCompareSession(m_compareView->compareSession());
    }
    for (const QString &cpath : compared)
    {
        const std::string key = cpath.toStdString();
        const std::string analysis = m_analyzer->resultText(cpath).toStdString();
        if (roi.isEmpty() && analysis.empty())
            continue;
        for (auto &folder : ws.folders)
            for (auto &img : folder.imageSet.images)
                if (img.filePath == key)
                {
                    img.roiX = roi.x;
                    img.roiY = roi.y;
                    img.roiW = roi.width;
                    img.roiH = roi.height;
                    img.analysis = analysis;
                    break;
                }
    }

    // M15 (Project): wrap the workspace in a Project that also captures the
    // analyzer pipeline and forward-compatible export/review/benchmark config,
    // so reopening the .mvproj restores the whole evaluation environment.
    mviewer::domain::Project proj;
    proj.name = QFileInfo(filePath).baseName().toStdString();
    proj.filePath = filePath.toStdString();
    proj.appVersion = "1.0.0";
    proj.createdIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    proj.modifiedIso = proj.createdIso;
    proj.workspace = ws;
    proj.datasetRoots = {currentDir().toStdString()};
    // M15 P0#3: list analyzers through the pipeline, not the registry directly.
    const AnalyzerPipeline pipeline;
    for (const auto &a : pipeline.analyzerIds())
        proj.analyzerPipeline.push_back(a);

    const std::string json = mviewer::core::serializeProject(proj);
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(json.c_str()) < 0)
    {
        QMessageBox::critical(this, "保存项目", "无法写入文件：" + filePath);
        return;
    }
    statusBar()->showMessage(QString("项目已保存: %1 (%2 张图片, %3 个目录)")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(static_cast<int>(ws.imageCount()))
                                 .arg(static_cast<int>(ws.folderCount())));
}

void MainWindow::openProject()
{
    const QString filePath =
        QFileDialog::getOpenFileName(this, "打开项目", QString(), "MViewer 项目 (*.mvproj)");
    if (filePath.isEmpty())
        return;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "打开项目", "无法读取文件：" + filePath);
        return;
    }
    const QByteArray data = f.readAll();
    mviewer::domain::Project proj;
    if (!mviewer::core::deserializeProject(std::string(data.constData(), data.size()), proj) ||
        proj.workspace.empty())
    {
        QMessageBox::critical(this, "打开项目", "项目文件无效或为空。");
        return;
    }

    // Reuse the workspace-restore path from openWorkspace() so the browsing view
    // + compare session + per-image ROI/analysis all come back from the .mvproj.
    const mviewer::domain::Workspace &ws = proj.workspace;
    const QString root = QString::fromStdString(ws.rootPath);
    changeDirectory(root);
    if (m_workspace)
    {
        m_workspace->setRootPath(root);
        QStringList cmp;
        for (const auto &p : ws.comparedImages)
            cmp.append(QString::fromStdString(p));
        m_workspace->setComparedImages(cmp);
        m_workspace->setCompareSessionJson(QString::fromStdString(ws.compareSessionJson));
    }

    QStringList comparePaths;
    comparePaths.reserve(static_cast<int>(ws.comparedImages.size()));
    for (const auto &p : ws.comparedImages)
        comparePaths.push_back(QString::fromStdString(p));

    m_analyzer->clearAllResults();
    for (const auto &folder : ws.folders)
        for (const auto &img : folder.imageSet.images)
            if (!img.analysis.empty())
                m_analyzer->setResult(QString::fromStdString(img.filePath),
                                      QString::fromStdString(img.analysis));

    std::optional<mviewer::domain::CompareSession> restoredSession;
    bool haveSession = false;
    if (!ws.compareSessionJson.empty())
    {
        restoredSession = mviewer::core::deserializeCompareSession(ws.compareSessionJson);
        haveSession = restoredSession.has_value();
    }
    if (!comparePaths.isEmpty())
    {
        openCompare(comparePaths);
        if (haveSession && m_compareView)
            m_compareView->applySession(*restoredSession);
    }

    std::string restoredPath;
    mviewer::domain::Selection restoredRoi;
    std::string restoredAnalysis;
    for (const auto &folder : ws.folders)
        for (const auto &img : folder.imageSet.images)
            if (restoredPath.empty() && (img.roiW > 0 || img.roiH > 0 || !img.analysis.empty()))
            {
                restoredRoi = {img.roiX, img.roiY, img.roiW, img.roiH};
                restoredAnalysis = img.analysis;
                restoredPath = img.filePath;
            }
    if (restoredPath.empty() && !comparePaths.isEmpty())
        restoredPath = comparePaths.first().toStdString();
    else if (restoredPath.empty() && ws.imageCount() > 0)
        restoredPath = ws.folders.front().imageSet.images.front().filePath;

    if (!restoredPath.empty())
    {
        m_selection->setCurrentImage(QString::fromStdString(restoredPath));
        if (m_imageViewer)
        {
            m_imageViewer->setImage(currentImagePath());
            m_previewPanel->setImage(currentImagePath());
        }
        if (!restoredAnalysis.empty())
            m_analysisPanel->setRegionStats(QString::fromStdString(restoredAnalysis));
        if (!restoredRoi.isEmpty() && m_compareView)
            m_compareView->applyROI(restoredRoi);
    }

    statusBar()->showMessage(QString("项目已打开: %1 (%2 张图片, %3 个目录)")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(static_cast<int>(ws.imageCount()))
                                 .arg(static_cast<int>(ws.folderCount())));
}

// ─── P0: product browse state (recent / favorites / history / restore) ────────

void MainWindow::pushHistory(const QString &path)
{
    if (path.isEmpty())
        return;
    // Drop any "forward" entries when a new navigation occurs (browser semantics).
    if (m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    if (!m_history.isEmpty() && m_history.last() == path)
        return; // no duplicate of the current tip
    m_history.append(path);
    m_historyIndex = m_history.size() - 1;
}

void MainWindow::navigateHistory(int delta)
{
    if (m_history.isEmpty())
        return;
    const int next = m_historyIndex + delta;
    if (next < 0 || next >= m_history.size())
        return;
    m_historyIndex = next;
    const QString path = m_history.at(next);
    // Re-open without pushing again (pushHistory is a no-op for the same tip).
    m_selection->setCurrentImage(path);
    m_imageViewer->setImage(path);  // async; imageReady() feeds AnalysisPanel
    m_previewPanel->setImage(path); // async; off UI thread
    statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
}

// P0: Directory-level back/forward history (independent of image history).
void MainWindow::pushDirHistory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    // Prune forward entries when branching.
    if (m_dirHistoryIndex >= 0 && m_dirHistoryIndex + 1 < m_dirHistory.size())
        m_dirHistory.erase(m_dirHistory.begin() + m_dirHistoryIndex + 1, m_dirHistory.end());
    // Suppress consecutive duplicates.
    if (!m_dirHistory.isEmpty() && m_dirHistory.last() == dir)
        return;
    m_dirHistory.append(dir);
    m_dirHistoryIndex = m_dirHistory.size() - 1;

    // Cap history size to avoid unbounded growth.
    constexpr int kMaxDirHistory = 50;
    while (m_dirHistory.size() > kMaxDirHistory)
    {
        m_dirHistory.removeFirst();
        --m_dirHistoryIndex;
    }
}

void MainWindow::goDirBack()
{
    if (m_dirHistoryIndex <= 0)
        return;
    --m_dirHistoryIndex;
    const QString dir = m_dirHistory.at(m_dirHistoryIndex);
    // Navigate without pushing to history (changeDirectory->pushDirHistory is guarded
    // by duplicate check). Use emitSignal=true to trigger the full update chain.
    m_directoryTree->navigateTo(dir, true);
}

void MainWindow::goDirForward()
{
    if (m_dirHistoryIndex < 0 || m_dirHistoryIndex + 1 >= m_dirHistory.size())
        return;
    ++m_dirHistoryIndex;
    const QString dir = m_dirHistory.at(m_dirHistoryIndex);
    m_directoryTree->navigateTo(dir, true);
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const auto &p : m_recent.items())
    {
        const QString qs = QString::fromStdString(p);
        auto *act = m_recentMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { changeDirectory(qs); });
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction("(无)")->setEnabled(false);
}

void MainWindow::rebuildRecentFilesMenu()
{
    if (!m_recentFileMenu)
        return;
    m_recentFileMenu->clear();
    for (const auto &p : m_recentFiles.items())
    {
        const QString qs = QString::fromStdString(p);
        auto *act = m_recentFileMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { onImageOpen(qs); });
    }
    if (m_recentFileMenu->isEmpty())
        m_recentFileMenu->addAction("(无)")->setEnabled(false);
}

void MainWindow::rebuildFavoritesMenu()
{
    if (!m_favMenu)
        return;
    m_favMenu->clear();
    for (const auto &qs : m_appState.favorites)
    {
        auto *act = m_favMenu->addAction(QFileInfo(qs).fileName());
        act->setToolTip(qs);
        connect(act, &QAction::triggered, this, [this, qs]() { changeDirectory(qs); });
    }
    if (m_favMenu->isEmpty())
        m_favMenu->addAction("(无)")->setEnabled(false);
}

void MainWindow::addFavoriteCurrent()
{
    if (currentDir().isEmpty())
    {
        statusBar()->showMessage("没有可收藏的目录");
        return;
    }
    m_appState.addFavorite(currentDir());
    m_directory->addFavorite(currentDir());
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    statusBar()->showMessage(QString("已收藏: %1").arg(currentDir()));
}

void MainWindow::removeFavorite(const QString &dir)
{
    const QString target = dir.isEmpty() ? currentDir() : dir;
    if (target.isEmpty())
        return;
    m_appState.removeFavorite(target);
    m_directory->removeFavorite(target);
    m_appState.save();
    rebuildFavoritesMenu();
    rebuildFavoritesBar();
    statusBar()->showMessage(QString("已取消收藏: %1").arg(QFileInfo(target).fileName()));
}

void MainWindow::rebuildFavoritesBar()
{
    if (!m_favoritesBar)
        return;
    m_favoritesBar->clear();
    for (const auto &qs : m_appState.favorites)
    {
        auto *item = new QListWidgetItem(QFileInfo(qs).fileName());
        item->setData(Qt::UserRole, qs);
        item->setToolTip(qs);
        m_favoritesBar->addItem(item);
    }
}

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
                m_thumbSizeSlider->setValue(ts);

            // P1-3: restore the Analysis workspace so the UI reopens where left off.
            if (m_analysisPanel)
            {
                m_analysisPanel->setVisible(m_appState.analysisVisible);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(m_appState.analysisVisible);
                m_analysisPanel->setCurrentPage(m_appState.analysisPage);
            }
            // Restore search panel visibility.
            const bool searchVisible = settings.value("searchVisible", true).toBool();
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
    m_appState.analysisVisible = m_analysisPanel && m_analysisPanel->isVisible();
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
        const QString recoveryPath =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
        QFile::remove(recoveryPath);
    }

    // Persist the recent-folders LRU alongside app state.
    const QString recentPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recent.json";
    QFile rf(recentPath);
    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        rf.write(QByteArray::fromStdString(m_recent.serialize()));

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
            settings.setValue("searchVisible", m_searchPanel->isVisible());
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
                cmpImg.append(QString::fromStdString(id));
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
    }

    QMainWindow::closeEvent(event);
}

// M15: decode a persisted compare-session JSON string into a value, or nullopt.
static std::optional<mviewer::domain::CompareSession> decodeCompareSession(const std::string &json)
{
    if (json.empty())
        return std::nullopt;
    return mviewer::core::deserializeCompareSession(json);
}

// M15: crash recovery — autosave current session to a recovery file.
void MainWindow::autosaveSession()
{
    if (currentDir().isEmpty() && currentImagePath().isEmpty())
        return;
    const QString recoveryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
    QFile f(recoveryPath);
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
            cmpImg.append(QString::fromStdString(id));
        obj.insert("compareImages", cmpImg);
        obj.insert("compareSession",
                   QString::fromStdString(mviewer::core::serializeCompareSession(cs)));
    }

    obj.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QJsonDocument doc(obj);
    f.write(doc.toJson());
    f.close();
}

// M15: crash recovery — restore session from recovery file if it exists.
void MainWindow::restoreSessionRecovery()
{
    const QString recoveryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
    QFile f(recoveryPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = f.readAll();
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

void MainWindow::checkForUpdates(bool silent)
{
    if (m_updateChecking)
        return;
    m_updateChecking = true;
    if (!silent)
        statusBar()->showMessage(tr("正在检查更新..."), 2000);

    // checkGitHub() performs a synchronous network request; run it off the GUI
    // thread and marshal the result back via the event loop. Guard `this` with
    // a QPointer so a window destroyed mid-request doesn't get a dangling call.
    QPointer<MainWindow> self(this);
    std::thread([self, silent]() {
        mviewer::core::UpdateChecker checker("1.0.4");
        checker.checkGitHub("lgxgizh/mviewer",
                            [self, silent](const mviewer::core::UpdateInfo &info) {
            QMetaObject::invokeMethod(qApp, [self, info, silent]() {
                if (!self)
                    return;
                self->m_updateChecking = false;
                self->onUpdateChecked(info, silent);
            });
        });
    }).detach();
}

void MainWindow::onUpdateChecked(const mviewer::core::UpdateInfo &info, bool silent)
{
    if (!info.error.empty())
    {
        if (!silent)
            QMessageBox::warning(this, tr("检查更新失败"),
                                 tr("无法获取更新信息：\n%1").arg(QString::fromStdString(info.error)));
        return;
    }
    if (info.hasUpdate)
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("发现新版本"));
        box.setText(tr("发现新版本 %1（当前 %2）。")
                        .arg(QString::fromStdString(info.latestVersion),
                             QString::fromStdString(info.currentVersion)));
        box.setInformativeText(tr("建议更新以获得最新功能与缺陷修复。"));
        QPushButton *openBtn = box.addButton(tr("前往下载页"), QMessageBox::AcceptRole);
        box.addButton(tr("稍后"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl(QString::fromStdString(info.releaseUrl)));
    }
    else if (!silent)
    {
        QMessageBox::information(this, tr("已是最新"),
                                 tr("当前已是最新版本（%1）。").arg(QString::fromStdString(info.currentVersion)));
    }
}

void MainWindow::maybeShowCrashReport()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/crash-reports";
    QDir d(dir);
    if (!d.exists())
        return;
    const QFileInfoList dumps = d.entryInfoList(QStringList() << "*.dmp", QDir::Files, QDir::Time);
    if (dumps.isEmpty())
        return;
    const QFileInfo &newest = dumps.first();

    // Only prompt once per crash dump (track last-seen mtime in QSettings).
    QSettings settings;
    const qint64 lastCheck = settings.value("crashReportLastCheck", 0).toLongLong();
    const qint64 mtime = newest.lastModified().toSecsSinceEpoch();
    if (mtime <= lastCheck)
        return;
    settings.setValue("crashReportLastCheck", QDateTime::currentSecsSinceEpoch());

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("崩溃报告"));
    box.setText(tr("检测到一次应用崩溃（%1）。\n崩溃转储已保存到：\n%2")
                    .arg(newest.lastModified().toString(), newest.absoluteFilePath()));
    box.setInformativeText(tr("可将此文件连同问题描述发送给开发者，以帮助定位并修复问题。"));
    QPushButton *openBtn = box.addButton(tr("打开崩溃目录"), QMessageBox::ActionRole);
    box.addButton(tr("忽略"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

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

void MainWindow::handleDroppedPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    // P1-5: drag workflow — dropping multiple images jumps straight into Compare
    // (no manual "add" button); a single image opens it; a directory opens the folder.
    if (paths.size() >= 2)
    {
        // All dropped items must be images (not a mix of dirs + files).
        bool allImages = true;
        for (const QString &p : paths)
            if (QFileInfo(p).isDir())
            {
                allImages = false;
                break;
            }
        if (allImages)
        {
            openCompare(paths);
            return;
        }
        // Mixed: open the first directory, ignore the rest.
    }
    // If first path is a directory, open it; otherwise open as images.
    const QFileInfo fi(paths.first());
    if (fi.isDir())
        changeDirectory(paths.first());
    else
        onImageOpen(paths.first());
}

void MainWindow::showMetadataOverlay()
{
    if (!m_metadataOverlay || currentImagePath().isEmpty())
        return;
    m_metadataOverlay->showForImage(currentImagePath());
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
        {
            m_metadataOverlay->showForImage(currentImagePath());
            // P0: Compute histogram from the viewer's already-decoded frame (lazy,
            // no extra decode) and pipe it into the overlay's mini histogram widget.
            if (m_imageViewer)
            {
                auto frame = m_imageViewer->frame();
                if (frame)
                    m_metadataOverlay->setHistogram(
                        mviewer::core::computeHistogram(frame->pixels()));
            }
        }
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
    // P0-3: keep the "图片信息" toggle in the View menu in sync so every entry
    // point (Ctrl+I, M key, ESC) agrees on the overlay's state.
    if (m_actToggleMetadata)
        m_actToggleMetadata->setChecked(show);
}

void MainWindow::copyCurrentImageToClipboard()
{
    if (currentImagePath().isEmpty())
        return;
    const QImage img(currentImagePath());
    if (!img.isNull())
        QApplication::clipboard()->setImage(img);
}

void MainWindow::toggleFullscreen()
{
    QWidget *target = m_imageViewer->isVisible() ? (QWidget *)m_imageViewer : (QWidget *)this;
    if (target->isFullScreen())
        target->showNormal();
    else
        target->showFullScreen();
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
    if (!m_imageViewer->isFullScreen())
        m_imageViewer->showFullScreen();
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
    return QMainWindow::eventFilter(watched, event);
}
