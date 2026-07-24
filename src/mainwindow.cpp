#include "mainwindow.h"

#include "application/OpenDirectoryUseCase.h"
#include "appstate.h"
#include "core/EventBus.h"
#include "core/RatingStore.h"
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
#include "core/export/ExportManager.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/RawMetadata.h"
#include "core/project/ProjectSerializer.h"
#include "core/workspace/WorkspaceSerializer.h"

#include "analysispanel.h"
#include "batchdialog.h"
#include "breadcrumbbar.h"
#include "compareworkspace.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/render/Viewport.h"
#include "directorytree.h"
#include "exportcommand.h"
#include "exportdialog.h"
#include "imageviewer.h"
#include "metadataoverlay.h"
#include "metadatapanel.h"
#include "pluginsettings.h"
#include "previewpanel.h"
#include "searchpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
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
#include <QHeaderView>
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
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

// M15 P0#1: forward declaration so openCompare() can restore a persisted
// session before the helper is defined later in this file.
static std::optional<mviewer::domain::CompareSession> decodeCompareSession(const std::string &json);

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    // P0-2: the single source of truth for the current image / selection. Every
    // panel reacts to this instead of tracking its own current index.
    m_selection = new SelectionModel(this);

    // P0: load persisted cross-session state + recent-folders LRU before UI.
    m_appState = AppState::load();
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
    fileMenu->addAction(m_actAddFavorite);

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
    viewMenu->addAction(m_actHistoryBack);
    viewMenu->addAction(m_actHistoryForward);
    viewMenu->addSeparator();
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
    m_actPluginSettings = new QAction("插件管理(&P)...", this);
    toolsMenu->addAction(m_actPluginSettings);

    // ----- 帮助(&H) -----
    auto *helpMenu = menuBar->addMenu("帮助(&H)");
    auto *actShortcuts = new QAction("键盘快捷键(&K)", this);
    actShortcuts->setShortcut(QKeySequence(Qt::Key_F1));
    connect(actShortcuts, &QAction::triggered, this, &MainWindow::showShortcutsHelp);
    helpMenu->addAction(actShortcuts);
    m_actAbout = new QAction("关于(&A)", this);
    helpMenu->addAction(m_actAbout);

    setMenuBar(menuBar);

    // ----- Breadcrumb navigation bar (M15 Product Shell P0) -----
    m_breadcrumb = new BreadcrumbBar(this);

    // ----- Left column: navigation sidebar + directory tree + preview -----
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    m_navSidebar = new QTreeWidget(leftWidget);
    m_navSidebar->setHeaderHidden(true);
    m_navSidebar->setMaximumHeight(180);
    m_navSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    buildNavSidebar();
    connect(m_navSidebar, &QTreeWidget::itemClicked, this, &MainWindow::onNavSidebarActivated);
    connect(m_navSidebar, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::onNavSidebarContextMenu);
    leftLayout->addWidget(m_navSidebar, 1);

    m_directoryTree = new DirectoryTree(leftWidget);
    m_directoryTree->installEventFilter(this);
    m_previewPanel = new PreviewPanel(leftWidget);
    m_previewPanel->installEventFilter(this);
    leftLayout->addWidget(m_directoryTree, 3);
    leftLayout->addWidget(m_previewPanel, 2);

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
    sortLayout->addWidget(sortCombo);

    // P0-2: View mode switcher (Grid / Large / Small / Detail / Filmstrip / Compact)
    auto *viewModeCombo = new QComboBox(sortBar);
    viewModeCombo->addItem("网格", ThumbnailPanel::Thumbnail);
    viewModeCombo->addItem("大图标", ThumbnailPanel::LargeIcon);
    viewModeCombo->addItem("小图标", ThumbnailPanel::SmallIcon);
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
    m_thumbnailPanel->installEventFilter(this);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // ----- Analysis panel (rightmost) + Metadata panel (M18, between gallery & analysis) -----
    m_analysisPanel = new AnalysisPanel(this);
    m_analysisPanel->installEventFilter(this);
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
    mainLayout->addWidget(centralSplitter, 1);
    setCentralWidget(mainContainer);

    // ----- Metadata overlay (P1: not in splitter, floats over main area) -----
    m_metadataPanel->setParent(this);
    m_metadataPanel->setWindowFlags(m_metadataPanel->windowFlags() | Qt::Tool);
    m_metadataPanel->setFixedSize(300, 440);
    m_metadataPanel->setStyleSheet(
        "MetadataPanel { background: palette(window); border: 1px solid palette(shadow); "
        "border-radius: 4px; }");
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
                if (!m_currentImagePath.isEmpty())
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
                m_currentDir = path;
                m_dirListDirty = true; // invalidate cache on dir change
                if (m_cachedImagePaths.isEmpty() || path != m_currentDir)
                {
                    // re-populate eagerly so the status bar reflects the count
                    m_cachedImagePaths.clear();
                    for (const auto &p :
                         OpenDirectoryUseCase::execute(path.toStdString()).imagePaths)
                        m_cachedImagePaths.append(QString::fromStdString(p));
                    m_dirListDirty = false;
                }
                // P0-1: record this folder in the recent-folders LRU + repopulate
                // the Recent menu and navigation sidebar.
                m_recent.add(path.toStdString());
                m_appState.addRecentFolder(path);
                refreshNavSidebar();
                rebuildRecentMenu();
                const int n = m_cachedImagePaths.size();
                statusBar()->showMessage(QString("目录: %1, 图片数: %2").arg(path).arg(n));
                // With no image selected yet, the title carries the folder.
                if (m_currentImagePath.isEmpty())
                    setWindowTitle(QString("%1 - MViewer").arg(QDir(path).dirName()));
                scheduleReindex();
            });

    connect(m_thumbnailPanel, &ThumbnailPanel::itemClicked, this,
            [this](const QString &path)
            {
                // P0-2: route selection through the shared model; all panels are
                // updated centrally in onCurrentImageChanged().
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
    // Dropping files directly onto the gallery behaves the same as dropping
    // them anywhere else on the window.
    connect(m_thumbnailPanel, &ThumbnailPanel::filesDropped, this, &MainWindow::handleDroppedPaths);
    // When the user deletes images from the gallery, advance the viewer off the
    // deleted image if it was the one being viewed.
    connect(m_thumbnailPanel, &ThumbnailPanel::pathsRemoved, this,
            [this](const QStringList &deleted)
            {
                if (m_currentImagePath.isEmpty() || m_imageViewer->isHidden())
                    return;
                if (!deleted.contains(m_currentImagePath))
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
                if (!m_currentImagePath.isEmpty())
                {
                    // Cap the per-image analysis map so it can't grow unbounded
                    // across a long session of browsing many images (M12.5 hygiene).
                    if (m_analysisByPath.size() > 500)
                        m_analysisByPath.erase(m_analysisByPath.begin());
                    m_analysisByPath.insert(m_currentImagePath, text);
                }
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
    connect(m_imageViewer, &ImageViewer::pixelInfo, this,
            [this](int x, int y, int r, int g, int b, int a, bool valid)
            {
                if (valid)
                {
                    if (a < 255)
                        statusBar()->showMessage(
                            QString("像素 [%1,%2]  RGBA(%3,%4,%5,%6)")
                                .arg(x).arg(y).arg(r).arg(g).arg(b).arg(a));
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
                {
                    m_currentDir = dir;
                    m_cachedImagePaths.clear();
                    m_dirListDirty = true;
                    m_thumbnailPanel->setDirectory(dir);
                    m_directoryTree->navigateTo(dir);
                    mviewer::core::SidecarStore::instance().importDirectory(dir.toStdString());
                }
            });
    connect(m_actOpenFile, &QAction::triggered, this,
            [this]()
            {
                const QString file = QFileDialog::getOpenFileName(
                    this, "打开图片",
                    m_currentDir.isEmpty() ? QString() : m_currentDir,
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
                QStringList imgs;
                if (!m_currentDir.isEmpty())
                {
                    for (const auto &p :
                         OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
                        imgs.append(QString::fromStdString(p));
                }
                if (imgs.isEmpty())
                {
                    const QString dir = QFileDialog::getExistingDirectory(this, tr("打开目录"));
                    if (!dir.isEmpty())
                    {
                        m_currentDir = dir;
                        m_cachedImagePaths.clear();
                        m_dirListDirty = true;
                        m_thumbnailPanel->setDirectory(dir);
                        m_directoryTree->navigateTo(dir);
                        mviewer::core::SidecarStore::instance().importDirectory(dir.toStdString());
                        for (const auto &p :
                             OpenDirectoryUseCase::execute(dir.toStdString()).imagePaths)
                            imgs.append(QString::fromStdString(p));
                    }
                }
                if (imgs.size() > 8)
                    imgs = imgs.mid(0, 8);
                if (!imgs.isEmpty())
                    openCompare(imgs);
            });
    connect(m_actToggleAnalysis, &QAction::triggered, m_analysisPanel, &QWidget::setVisible);
    connect(m_actToggleSearch, &QAction::triggered, m_searchPanel, &QWidget::setVisible);
    // P0-3: metadata overlay toggle — show/hide the semi-transparent overlay.
    connect(m_actToggleMetadata, &QAction::triggered, this,
            [this](bool checked)
            {
                if (!m_metadataOverlay || m_currentImagePath.isEmpty())
                    return;
                if (checked)
                    m_metadataOverlay->showForImage(m_currentImagePath);
                else
                    m_metadataOverlay->hide();
            });
    connect(m_searchPanel, &SearchPanel::resultActivated, this,
            QOverload<const QString &>::of(&MainWindow::onImageOpen));
    connect(m_actBatch, &QAction::triggered, this,
            [this]()
            {
                if (!m_batchDialog)
                    m_batchDialog = new BatchDialog(this);
                // Pre-fill with current directory's images.
                m_batchDialog->setInputFiles(m_cachedImagePaths);
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
                }
                m_pluginSettings->show();
                m_pluginSettings->raise();
                m_pluginSettings->activateWindow();
            });
    connect(
        m_actAbout, &QAction::triggered, this, [this]()
        { QMessageBox::about(this, "关于 MViewer", "MViewer\n\n一个简单的图片查看与分析工具。"); });

    // P0: recent / favorites / history wiring.
    connect(m_actAddFavorite, &QAction::triggered, this, &MainWindow::addFavoriteCurrent);
    connect(m_actHistoryBack, &QAction::triggered, this, [this]() { navigateHistory(-1); });
    connect(m_actHistoryForward, &QAction::triggered, this, [this]() { navigateHistory(1); });

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
            if (!m_currentImagePath.isEmpty())
                onImageOpen(m_currentImagePath);
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
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // P0-1: F5 refreshes the directory tree and the gallery from disk.
    if (event->key() == Qt::Key_F5 && !mod)
    {
        m_directoryTree->refresh();
        m_thumbnailPanel->refresh();
        m_dirListDirty = true;
        scheduleReindex();
        event->accept();
        return;
    }
    // P0-3: ESC dismisses the metadata overlay (keeps the image area maximal for
    // browsing). Only consume the key when the overlay is actually showing.
    if (event->key() == Qt::Key_Escape && !mod && m_metadataOverlay &&
        m_metadataOverlay->isVisible())
    {
        m_metadataOverlay->hide();
        if (m_actToggleMetadata)
            m_actToggleMetadata->setChecked(false);
        event->accept();
        return;
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
    // P0-3 / P1-4: 'M' key toggles the metadata overlay on the image viewer.
    if (event->key() == Qt::Key_M && !mod)
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
            ThumbnailPanel::Thumbnail, ThumbnailPanel::LargeIcon, ThumbnailPanel::SmallIcon,
            ThumbnailPanel::Details,   ThumbnailPanel::Filmstrip, ThumbnailPanel::Compact};
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
        m_navSidebar->setVisible(!visible);
        event->accept();
        return;
    }
    // P1-4: Ctrl+C copies the current image to clipboard; Ctrl+Shift+C copies its path.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        if ((mod & Qt::ShiftModifier))
        {
            if (!m_currentImagePath.isEmpty())
                QApplication::clipboard()->setText(m_currentImagePath);
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

    for (const QString &p : m_cachedImagePaths)
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
    if (m_currentImagePath.isEmpty())
        return;
    mviewer::core::RatingStore::instance().setRating(m_currentImagePath.toStdString(), stars);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(m_currentImagePath); // refresh the rating widget
    mviewer::core::SidecarStore::instance().writeSidecar(m_currentImagePath.toStdString());
    statusBar()->showMessage(
        QString("已为 %1 评分: %2 星").arg(QFileInfo(m_currentImagePath).fileName()).arg(stars));
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
    if (m_currentImagePath.isEmpty())
        return;
    mviewer::core::RatingStore::instance().setColorLabel(m_currentImagePath.toStdString(), label);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(m_currentImagePath);
    mviewer::core::SidecarStore::instance().writeSidecar(m_currentImagePath.toStdString());
    const QString name = QFileInfo(m_currentImagePath).fileName();
    statusBar()->showMessage(label == 0 ? QString("已清除 %1 的色标").arg(name)
                                        : QString("已为 %1 设置色标 %2").arg(name).arg(label));
}

void MainWindow::toggleCurrentPick()
{
    if (m_currentImagePath.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.picked(m_currentImagePath.toStdString());
    rs.setPicked(m_currentImagePath.toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(m_currentImagePath);
    mviewer::core::SidecarStore::instance().writeSidecar(m_currentImagePath.toStdString());
    statusBar()->showMessage(
        v ? QString("已收藏 %1").arg(QFileInfo(m_currentImagePath).fileName())
          : QString("已取消收藏 %1").arg(QFileInfo(m_currentImagePath).fileName()));
}

void MainWindow::toggleCurrentReject()
{
    if (m_currentImagePath.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    const bool v = !rs.rejected(m_currentImagePath.toStdString());
    rs.setRejected(m_currentImagePath.toStdString(), v);
    m_thumbnailPanel->invalidateRatings();
    m_metadataPanel->setImage(m_currentImagePath);
    mviewer::core::SidecarStore::instance().writeSidecar(m_currentImagePath.toStdString());
    statusBar()->showMessage(
        v ? QString("已拒绝 %1").arg(QFileInfo(m_currentImagePath).fileName())
          : QString("已取消拒绝 %1").arg(QFileInfo(m_currentImagePath).fileName()));
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
    refreshNavSidebar();
    // M14-1: track in recent-files LRU + refresh menu.
    m_recentFiles.add(path.toStdString());
    rebuildRecentFilesMenu();
    // M12.2 (G2-ext): if this image had a saved analysis in the workspace, restore
    // it to the panel (e.g. after reopening a .mvws with per-image analysis).
    const auto it = m_analysisByPath.find(path);
    if (it != m_analysisByPath.end() && !it->isEmpty())
        m_analysisPanel->setRegionStats(*it);
    if (wasHidden)
        m_imageViewer->show();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
}

void MainWindow::onCurrentImageChanged(const QString &path)
{
    // P0-2: the ONE place that fans the current-image change out to every view.
    m_currentImagePath = path; // keep the mirror for existing readers
    if (path.isEmpty())
        return;

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

    mviewer::core::RatingStore::instance().addRecent(path.toStdString()); // P3 recents

    // Window title + status bar identity follow the current image.
    const QFileInfo fi(path);
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

void MainWindow::openCompare(const QStringList &images, const QString &sessionJson)
{
    QStringList imgs = images;
    if (imgs.isEmpty())
    {
        if (m_currentDir.isEmpty())
            return;
        for (const auto &p : OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
            imgs.append(QString::fromStdString(p));
    }
    if (imgs.isEmpty())
        return;

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("比较模式 - MViewer");
    dlg->resize(1000, 700);

    auto *layout = new QVBoxLayout(dlg);
    m_compareView = new CompareWorkspace(dlg);
    layout->addWidget(m_compareView);
    m_compareView->setImages(imgs);
    connect(m_compareView, &CompareWorkspace::pixelInfo, this,
            [this](const QString &text) { statusBar()->showMessage(text); });

    // M15 P0#1: if a persisted compare session was supplied, restore it *after*
    // the images are loaded (applySession needs the engine + cell views alive).
    if (!sessionJson.isEmpty() && m_compareView)
    {
        const auto session = decodeCompareSession(sessionJson.toStdString());
        if (session)
            m_compareView->applySession(*session);
    }

    // P0-1: guard m_compareView lifetime — when the compare dialog is closed
    // (WA_DeleteOnClose), reset the pointer so every downstream accessor
    // (saveWorkspace, saveProject, exportReport, autosaveSession) is safe.
    connect(dlg, &QDialog::destroyed, this,
            [this]()
            {
                m_compareView = nullptr;
                disconnect(m_compareDestroyConnection);
            });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::navigate(int delta)
{
    if (m_currentDir.isEmpty() || m_currentImagePath.isEmpty())
        return;

    // Use cached directory list (no re-scan on arrow key press)
    if (m_dirListDirty)
    {
        m_cachedImagePaths.clear();
        for (const auto &p : OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
            m_cachedImagePaths.append(QString::fromStdString(p));
        m_dirListDirty = false;
    }
    QStringList list = m_cachedImagePaths;
    if (list.isEmpty())
        return;

    int idx = list.indexOf(m_currentImagePath);
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
    if (m_currentDir.isEmpty())
        return;
    if (m_dirListDirty)
    {
        m_cachedImagePaths.clear();
        for (const auto &p : OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
            m_cachedImagePaths.append(QString::fromStdString(p));
        m_dirListDirty = false;
    }
    const QStringList &list = m_cachedImagePaths;
    if (list.isEmpty())
        return;

    int idx = list.indexOf(m_currentImagePath);
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
    if (target != idx || m_currentImagePath.isEmpty())
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
        "<tr><td><kbd>Ctrl+1</kbd>…<kbd>Ctrl+6</kbd></td><td>缩略图 / 大图标 / 小图标 / 详情 / "
        "胶片 / 紧凑</td></tr>"
        "<tr><th colspan='2'>比较</th></tr>"
        "<tr><td><kbd>C</kbd></td><td>比较模式</td></tr>"
        "<tr><td><kbd>Space</kbd></td><td>快速比较当前 + 下一张</td></tr>"
        "<tr><td><kbd>1</kbd>…<kbd>6</kbd>（比较窗口内）</td><td>切换布局预设</td></tr>"
        "<tr><td><kbd>ESC</kbd>（比较窗口内）</td><td>关闭比较窗口</td></tr>"
        "<tr><th colspan='2'>分析 / 信息</th></tr>"
        "<tr><td><kbd>H</kbd></td><td>直方图 / 分析面板</td></tr>"
        "<tr><td><kbd>M</kbd> / <kbd>Ctrl+I</kbd></td><td>图片信息浮层（浮层内 Ctrl+C "
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
        m_directoryTree->navigateTo(path, true);
}

void MainWindow::exportReport()
{
    // M14-4: collect current view data and build an HTML report.
    mviewer::core::ReportContext ctx;
    ctx.title = "MViewer Analysis Report";

    if (m_currentImagePath.isEmpty())
    {
        QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
        return;
    }
    ctx.imagePath = m_currentImagePath.toStdString();

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
                                                     tr("HTML 文件 (*.html);;JSON 文件 (*.json)"));
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
    else
    {
        f.write(QByteArray::fromStdString(html));
    }
    f.close();
    QMessageBox::information(this, tr("导出报告"), tr("已导出：%1").arg(out));
}

void MainWindow::exportImages()
{
    // M17: export selected paths, or the filtered/visible set, or the full directory.
    QStringList paths = m_thumbnailPanel->selectedPaths();
    if (paths.isEmpty())
        paths = m_thumbnailPanel->visiblePaths(); // filtered set
    if (paths.isEmpty())
        paths = m_thumbnailPanel->pathList(); // fallback: all
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先打开一个图片目录。"));
        return;
    }

    ExportDialog dlg(this);
    dlg.setSources(paths);
    dlg.exec();
}

void MainWindow::saveWorkspace()
{
    if (m_currentDir.isEmpty())
    {
        QMessageBox::information(this, "保存工作区", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存工作区", m_currentDir + "/workspace.mvws", "MViewer 工作区 (*.mvws)");
    if (filePath.isEmpty())
        return;

    // Build the domain model from the real directory (recursive, no pixel
    // decode) using the existing, tested ImageRepository::loadWorkspace.
    mviewer::domain::Workspace ws =
        ImageRepository::instance().loadWorkspace(m_currentDir.toStdString());
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
        const auto it = m_analysisByPath.find(cpath);
        const std::string analysis =
            (it != m_analysisByPath.end()) ? it->toStdString() : std::string();
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
    const QString root = QString::fromStdString(ws.rootPath);
    m_currentDir = root;
    m_cachedImagePaths.clear();
    m_dirListDirty = true;
    m_thumbnailPanel->setDirectory(root);
    m_directoryTree->navigateTo(root);

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
    m_analysisByPath.clear();
    for (const auto &folder : ws.folders)
    {
        for (const auto &img : folder.imageSet.images)
        {
            if (!img.analysis.empty())
                m_analysisByPath.insert(QString::fromStdString(img.filePath),
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
        m_currentImagePath = QString::fromStdString(restoredPath);
        if (m_imageViewer)
        {
            // Async decode; imageReady() feeds AnalysisPanel once the frame is
            // ready (no synchronous frame() read here — it isn't ready yet).
            m_imageViewer->setImage(m_currentImagePath);
            m_previewPanel->setImage(m_currentImagePath);
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
    if (m_currentDir.isEmpty())
    {
        QMessageBox::information(this, "保存项目", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存项目", m_currentDir + "/project.mvproj", "MViewer 项目 (*.mvproj)");
    if (filePath.isEmpty())
        return;

    // Build the workspace exactly like saveWorkspace (datasets + compared images
    // + compare-session snapshot + per-image ROI/analysis).
    mviewer::domain::Workspace ws =
        ImageRepository::instance().loadWorkspace(m_currentDir.toStdString());
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
        const auto it = m_analysisByPath.find(cpath);
        const std::string analysis =
            (it != m_analysisByPath.end()) ? it->toStdString() : std::string();
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
    proj.datasetRoots = {m_currentDir.toStdString()};
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
    m_currentDir = root;
    m_cachedImagePaths.clear();
    m_dirListDirty = true;
    m_thumbnailPanel->setDirectory(root);
    m_directoryTree->navigateTo(root);

    QStringList comparePaths;
    comparePaths.reserve(static_cast<int>(ws.comparedImages.size()));
    for (const auto &p : ws.comparedImages)
        comparePaths.push_back(QString::fromStdString(p));

    m_analysisByPath.clear();
    for (const auto &folder : ws.folders)
        for (const auto &img : folder.imageSet.images)
            if (!img.analysis.empty())
                m_analysisByPath.insert(QString::fromStdString(img.filePath),
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
        m_currentImagePath = QString::fromStdString(restoredPath);
        if (m_imageViewer)
        {
            m_imageViewer->setImage(m_currentImagePath);
            m_previewPanel->setImage(m_currentImagePath);
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
    m_currentImagePath = path;
    m_imageViewer->setImage(path);  // async; imageReady() feeds AnalysisPanel
    m_previewPanel->setImage(path); // async; off UI thread
    statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
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
        connect(act, &QAction::triggered, this,
                [this, qs]()
                {
                    m_currentDir = qs;
                    m_cachedImagePaths.clear();
                    m_dirListDirty = true;
                    m_thumbnailPanel->setDirectory(qs);
                    m_directoryTree->navigateTo(qs);
                });
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
        connect(act, &QAction::triggered, this,
                [this, qs]()
                {
                    m_currentDir = qs;
                    m_cachedImagePaths.clear();
                    m_dirListDirty = true;
                    m_thumbnailPanel->setDirectory(qs);
                    m_directoryTree->navigateTo(qs);
                });
    }
    if (m_favMenu->isEmpty())
        m_favMenu->addAction("(无)")->setEnabled(false);
}

void MainWindow::addFavoriteCurrent()
{
    if (m_currentDir.isEmpty())
    {
        statusBar()->showMessage("没有可收藏的目录");
        return;
    }
    m_appState.addFavorite(m_currentDir);
    m_appState.save();
    rebuildFavoritesMenu();
    statusBar()->showMessage(QString("已收藏: %1").arg(m_currentDir));
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

            // P1-3: restore the Analysis workspace + nav sidebar visibility so the
            // UI reopens exactly where the user left off.
            if (m_analysisPanel)
            {
                m_analysisPanel->setVisible(m_appState.analysisVisible);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(m_appState.analysisVisible);
                m_analysisPanel->setCurrentPage(m_appState.analysisPage);
            }
            if (m_navSidebar)
                m_navSidebar->setVisible(m_appState.navSidebarVisible);
            // Restore search panel visibility.
            const bool searchVisible =
                settings.value("searchVisible", true).toBool();
            if (m_searchPanel)
                m_searchPanel->setVisible(searchVisible);
            if (m_actToggleSearch)
                m_actToggleSearch->setChecked(searchVisible);

            const QString dir = m_appState.lastDir;
            if (dir.isEmpty() || !QDir(dir).exists())
                return;
            m_currentDir = dir;
            m_cachedImagePaths.clear();
            m_dirListDirty = true;
            m_thumbnailPanel->setDirectory(dir);
            m_directoryTree->navigateTo(dir);

            const QString img = m_appState.lastImage;
            if (!img.isEmpty() && QFile::exists(img))
            {
                pushHistory(img);
                m_currentImagePath = img;
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
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Persist browse position for next launch (P0 cross-session restore).
    m_appState.lastDir = m_currentDir;
    m_appState.lastImage = m_currentImagePath;
    m_appState.lastThumbScroll = m_thumbnailPanel ? m_thumbnailPanel->scrollOffset() : 0;

    // P1-3: persist the Analysis workspace + nav sidebar so reopening the app
    // restores the full UI state, not just the last image.
    m_appState.analysisVisible = m_analysisPanel && m_analysisPanel->isVisible();
    m_appState.analysisPage = m_analysisPanel ? m_analysisPanel->currentPage() : 0;
    m_appState.navSidebarVisible = m_navSidebar && m_navSidebar->isVisible();

    // P1-3: persist the navigation history stack (browser back/forward + History
    // panel) so reopening restores exactly where the user was browsing.
    m_appState.navHistory = m_history;
    m_appState.navHistoryIndex = m_historyIndex;
    m_appState.save();

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
        if (m_imageViewer && !m_imageViewer->isHidden() && !m_currentImagePath.isEmpty())
        {
            const auto v = m_imageViewer->viewTransform();
            settings.setValue("viewerPath", m_currentImagePath);
            settings.setValue("viewerScale", v.scale);
            settings.setValue("viewerOffX", v.offsetX);
            settings.setValue("viewerOffY", v.offsetY);
        }
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
    if (m_currentDir.isEmpty() && m_currentImagePath.isEmpty())
        return;
    const QString recoveryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
    QFile f(recoveryPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    // Simple JSON: lastDir, lastImage, lastThumbScroll, compare (M15 P0#1)
    QJsonObject obj;
    obj.insert("lastDir", m_currentDir);
    obj.insert("lastImage", m_currentImagePath);
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
                m_currentDir = lastDir;
                m_cachedImagePaths.clear();
                m_dirListDirty = true;
                m_thumbnailPanel->setDirectory(lastDir);
                m_directoryTree->navigateTo(lastDir);
                if (lastThumbScroll > 0)
                    m_thumbnailPanel->verticalScrollBar()->setValue(lastThumbScroll);
            }
            if (!lastImage.isEmpty() && QFile::exists(lastImage))
            {
                m_currentImagePath = lastImage;
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
    {
        m_currentDir = paths.first();
        m_cachedImagePaths.clear();
        m_dirListDirty = true;
        m_thumbnailPanel->setDirectory(paths.first());
        m_directoryTree->navigateTo(paths.first());
    }
    else
    {
        onImageOpen(paths.first());
    }
}

void MainWindow::showMetadataOverlay()
{
    if (!m_metadataOverlay || m_currentImagePath.isEmpty())
        return;
    m_metadataOverlay->showForImage(m_currentImagePath);
}

void MainWindow::toggleMetadataOverlay()
{
    if (!m_metadataOverlay || m_currentImagePath.isEmpty())
        return;
    const bool show = !m_metadataOverlay->isVisible();
    if (show)
        m_metadataOverlay->showForImage(m_currentImagePath);
    else
        m_metadataOverlay->hide();
    // P0-3: keep the "图片信息" toggle in the View menu in sync so every entry
    // point (Ctrl+I, M key, ESC) agrees on the overlay's state.
    if (m_actToggleMetadata)
        m_actToggleMetadata->setChecked(show);
}

void MainWindow::copyCurrentImageToClipboard()
{
    if (m_currentImagePath.isEmpty())
        return;
    const QImage img(m_currentImagePath);
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
    if (m_currentImagePath.isEmpty() || m_currentDir.isEmpty())
    {
        statusBar()->showMessage("请先选择一张图片再开始幻灯片放映", 3000);
        if (m_actSlideshow)
            m_actSlideshow->setChecked(false);
        return;
    }
    // Fullscreen the viewer for the slideshow; ESC (or S) stops it.
    onImageOpen(m_currentImagePath);
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
        QString("幻灯片放映中 — 按 S 或 ESC 停止 (间隔 %1 秒)").arg(interval / 1000.0, 0, 'f', 1), 3000);
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
    if (m_currentImagePath.isEmpty())
        return;
    if (m_dirListDirty)
    {
        m_cachedImagePaths.clear();
        for (const auto &p : OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
            m_cachedImagePaths.append(QString::fromStdString(p));
        m_dirListDirty = false;
    }
    QStringList imgs;
    imgs << m_currentImagePath;
    const int idx = m_cachedImagePaths.indexOf(m_currentImagePath);
    if (idx >= 0 && idx + 1 < m_cachedImagePaths.size())
        imgs << m_cachedImagePaths.at(idx + 1);
    else if (idx != 0 && !m_cachedImagePaths.isEmpty())
        imgs << m_cachedImagePaths.first();
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
        if (event->type() == QEvent::MouseButtonPress && !m_currentImagePath.isEmpty())
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
            if (m_metadataHoverTimer && !m_currentImagePath.isEmpty())
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

// P0-1: build the Explorer-like navigation sidebar with Favorites / Recent / History.
void MainWindow::buildNavSidebar()
{
    if (!m_navSidebar)
        return;
    m_navSidebar->clear();

    auto *favRoot = new QTreeWidgetItem(m_navSidebar, QStringList{"收藏夹"});
    favRoot->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirHomeIcon));
    favRoot->setData(0, Qt::UserRole, "__favorites");
    for (const QString &dir : m_appState.favorites)
    {
        auto *it = new QTreeWidgetItem(favRoot, QStringList{QFileInfo(dir).fileName()});
        it->setToolTip(0, dir);
        it->setData(0, Qt::UserRole, dir);
        it->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    }
    favRoot->setExpanded(true);

    auto *recentRoot = new QTreeWidgetItem(m_navSidebar, QStringList{"最近访问"});
    recentRoot->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    recentRoot->setData(0, Qt::UserRole, "__recent");
    for (const QString &dir : m_appState.recentFolders)
    {
        auto *it = new QTreeWidgetItem(recentRoot, QStringList{QFileInfo(dir).fileName()});
        it->setToolTip(0, dir);
        it->setData(0, Qt::UserRole, dir);
        it->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    }
    recentRoot->setExpanded(true);

    auto *histRoot = new QTreeWidgetItem(m_navSidebar, QStringList{"历史图片"});
    histRoot->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView));
    histRoot->setData(0, Qt::UserRole, "__history");
    for (const QString &img : m_appState.history)
    {
        auto *it = new QTreeWidgetItem(histRoot, QStringList{QFileInfo(img).fileName()});
        it->setToolTip(0, img);
        it->setData(0, Qt::UserRole, img);
        it->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    }
    histRoot->setExpanded(false);
}

void MainWindow::refreshNavSidebar()
{
    buildNavSidebar();
}

void MainWindow::onNavSidebarActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const QString data = item->data(0, Qt::UserRole).toString();
    if (data.startsWith("__"))
        return; // root nodes are not navigable directly
    if (QFileInfo(data).isDir())
        openDirectory(data);
    else if (QFileInfo(data).isFile())
        onImageOpen(data);
}

void MainWindow::onNavSidebarContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_navSidebar->itemAt(pos);
    if (!item)
        return;
    const QString data = item->data(0, Qt::UserRole).toString();
    if (data.startsWith("__"))
        return;

    QMenu menu(this);
    QAction *actAdd = menu.addAction("添加到收藏夹");
    QAction *actRemove = menu.addAction("从收藏夹移除");
    QAction *actClearRecent = menu.addAction("清空最近访问");
    QAction *actClearHistory = menu.addAction("清空历史图片");

    QAction *chosen = menu.exec(m_navSidebar->mapToGlobal(pos));
    if (chosen == actAdd)
    {
        m_appState.addFavorite(data);
        refreshNavSidebar();
        rebuildFavoritesMenu();
    }
    else if (chosen == actRemove)
    {
        m_appState.removeFavorite(data);
        refreshNavSidebar();
        rebuildFavoritesMenu();
    }
    else if (chosen == actClearRecent)
    {
        m_appState.clearRecentFolders();
        refreshNavSidebar();
        rebuildRecentMenu();
    }
    else if (chosen == actClearHistory)
    {
        m_appState.clearHistory();
        refreshNavSidebar();
        // History is only shown in the sidebar, no separate menu yet.
    }
}
