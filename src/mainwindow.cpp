#include "mainwindow.h"

#include "application/OpenDirectoryUseCase.h"
#include "appstate.h"
#include "core/EventBus.h"
#include "core/command/CallbackCommand.h"
#include "core/command/CompareCommand.h"
#include "core/command/DeleteCommand.h"
#include "core/command/OpenDirectoryCommand.h"
#include "core/command/RenameCommand.h"
#include "core/command/ToggleHistogramCommand.h"
#include "core/image/ImageRepository.h"
#include "core/workspace/WorkspaceSerializer.h"

#include "analysispanel.h"
#include "compareworkspace.h"
#include "directorytree.h"
#include "exportcommand.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
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

    // M13.5: restore persisted window geometry/layout (QSettings, independent of workspace).
    {
        QSettings settings;
        if (settings.contains("geometry"))
            restoreGeometry(settings.value("geometry").toByteArray());
        if (settings.contains("windowState"))
            restoreState(settings.value("windowState").toByteArray());
    }

    // P0: restore last folder + image + scroll position (deferred to event loop).
    rebuildFavoritesMenu();
    restoreLastSession();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    auto *menuBar = new QMenuBar(this);

    // ----- 文件(&F) -----
    auto *fileMenu = menuBar->addMenu("文件(&F)");
    m_actOpenDir = new QAction("打开目录(&O)", this);
    m_actSaveWorkspace = new QAction("保存工作区(&S)", this);
    m_actOpenWorkspace = new QAction("打开工作区(&W)", this);
    m_actExit = new QAction("退出(&Q)", this);
    fileMenu->addAction(m_actOpenDir);
    fileMenu->addSeparator();

    // P0: Recent folders (from core::RecentFiles LRU) + Favorites (pinned).
    m_recentMenu = fileMenu->addMenu("最近目录(&R)");
    m_favMenu = fileMenu->addMenu("收藏目录(&F)");
    m_actAddFavorite = new QAction("收藏当前目录(&D)", this);
    m_actAddFavorite->setShortcut(QKeySequence("Ctrl+D")); // Ctrl+D
    fileMenu->addAction(m_actAddFavorite);

    fileMenu->addSeparator();
    fileMenu->addAction(m_actSaveWorkspace);
    fileMenu->addAction(m_actOpenWorkspace);
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

    // ----- 帮助(&H) -----
    auto *helpMenu = menuBar->addMenu("帮助(&H)");
    m_actAbout = new QAction("关于(&A)", this);
    helpMenu->addAction(m_actAbout);

    setMenuBar(menuBar);

    // ----- Left column: directory tree (top) + preview (bottom) -----
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    m_directoryTree = new DirectoryTree(leftWidget);
    m_previewPanel = new PreviewPanel(leftWidget);
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
    sortCombo->addItem("文件名", ThumbnailPanel::SortName);
    sortCombo->addItem("日期", ThumbnailPanel::SortDate);
    sortCombo->addItem("大小", ThumbnailPanel::SortSize);
    sortCombo->addItem("分辨率", ThumbnailPanel::SortResolution);
    sortLayout->addWidget(sortCombo);

    // M18: live search bar.
    sortLayout->addWidget(new QLabel("搜索：", sortBar));
    m_searchEdit = new QLineEdit(sortBar);
    m_searchEdit->setPlaceholderText("按文件名过滤...");
    m_searchEdit->setClearButtonEnabled(true);
    sortLayout->addWidget(m_searchEdit, 1);
    m_searchRecursive = new QCheckBox("包含子目录", sortBar);
    sortLayout->addWidget(m_searchRecursive);
    sortLayout->addStretch(0);
    rightLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(rightWidget);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // ----- Analysis panel (rightmost) + Metadata panel (M18, between gallery & analysis) -----
    m_analysisPanel = new AnalysisPanel(this);
    m_metadataPanel = new MetadataPanel(this);

    // ----- 4-way horizontal split: left | gallery | metadata | analysis -----
    auto *centralSplitter = new QSplitter(Qt::Horizontal, this);
    centralSplitter->addWidget(leftWidget);
    centralSplitter->addWidget(rightWidget);
    centralSplitter->addWidget(m_metadataPanel);
    centralSplitter->addWidget(m_analysisPanel);
    centralSplitter->setStretchFactor(0, 0);
    centralSplitter->setStretchFactor(1, 1);
    centralSplitter->setStretchFactor(2, 0);
    centralSplitter->setStretchFactor(3, 0);
    centralSplitter->setSizes({340, 820, 300, 320});
    setCentralWidget(centralSplitter);

    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle("图片查看 - MViewer");

    // ----- Signals -----
    connect(m_directoryTree, &DirectoryTree::directoryChanged, m_thumbnailPanel,
            &ThumbnailPanel::setDirectory);
    connect(m_directoryTree, &DirectoryTree::directoryChanged, this,
            [this](const QString &path)
            {
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
                // P0: record this folder in the recent-folders LRU + repopulate
                // the Recent menu so it is always one click away.
                m_recent.add(path.toStdString());
                rebuildRecentMenu();
                const int n = m_cachedImagePaths.size();
                statusBar()->showMessage(QString("目录: %1, 图片数: %2").arg(path).arg(n));
            });

    connect(m_thumbnailPanel, &ThumbnailPanel::itemClicked, this,
            [this](const QString &path)
            {
                m_previewPanel->setImage(path);       // async decode (off UI thread)
                m_metadataPanel->setImage(path);      // M18: show metadata
                statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpen(path); });
    connect(m_thumbnailPanel, &ThumbnailPanel::compareRequested, this, &MainWindow::openCompare);

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
            [this](int x, int y, int r, int g, int b, bool valid)
            {
                if (valid)
                    statusBar()->showMessage(
                        QString("像素 [%1,%2]  RGB(%3,%4,%5)").arg(x).arg(y).arg(r).arg(g).arg(b));
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
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString &)
            {
                m_thumbnailPanel->setFilter(m_searchEdit->text(), m_searchRecursive->isChecked());
            });
    connect(m_searchRecursive, &QCheckBox::toggled, this,
            [this](bool)
            {
                m_thumbnailPanel->setFilter(m_searchEdit->text(), m_searchRecursive->isChecked());
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
                }
            });
    connect(m_actSaveWorkspace, &QAction::triggered, this, &MainWindow::saveWorkspace);
    connect(m_actOpenWorkspace, &QAction::triggered, this, &MainWindow::openWorkspace);
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
    connect(
        m_actAbout, &QAction::triggered, this, [this]()
        { QMessageBox::about(this, "关于 MViewer", "MViewer\n\n一个简单的图片查看与分析工具。"); });

    // P0: recent / favorites / history wiring.
    connect(m_actAddFavorite, &QAction::triggered, this, &MainWindow::addFavoriteCurrent);
    connect(m_actHistoryBack, &QAction::triggered, this, [this]() { navigateHistory(-1); });
    connect(m_actHistoryForward, &QAction::triggered, this, [this]() { navigateHistory(1); });

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
        "quick_preview", "快速预览 (Space)",
        [this]()
        {
            if (!m_currentImagePath.isEmpty())
                onImageOpen(m_currentImagePath);
        },
        std::vector<CommandShortcut>{{Qt::Key_Space, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "fullscreen", "全屏 (F)",
        [this]()
        {
            QWidget *target =
                m_imageViewer->isVisible() ? (QWidget *)m_imageViewer : (QWidget *)this;
            if (target->isFullScreen())
                target->showNormal();
            else
                target->showFullScreen();
        },
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
    ICommand *cmd = CommandRegistry::instance().findByShortcut(
        event->key(), static_cast<int>(event->modifiers()));
    if (cmd)
    {
        cmd->execute();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::onImageOpen(const QString &path)
{
    m_previewPanel->setImage(path); // async decode (off UI thread)
    m_imageViewer->setImage(path);  // async decode; imageReady() feeds AnalysisPanel
    m_metadataPanel->setImage(path); // M18: show metadata for the opened image
    m_currentImagePath = path;
    pushHistory(path); // P0: in-session browse history
    // M12.2 (G2-ext): if this image had a saved analysis in the workspace, restore
    // it to the panel (e.g. after reopening a .mvws with per-image analysis).
    const auto it = m_analysisByPath.find(path);
    if (it != m_analysisByPath.end() && !it->isEmpty())
        m_analysisPanel->setRegionStats(*it);
    statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
    if (m_imageViewer->isHidden())
        m_imageViewer->show();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
}

void MainWindow::openCompare(const QStringList &images)
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
    const int next = idx + delta;
    if (next < 0 || next >= list.size())
        return;

    const QString path = list.at(next);
    m_currentImagePath = path;
    m_imageViewer->setImage(path);  // async; imageReady() feeds AnalysisPanel
    m_previewPanel->setImage(path); // async; off UI thread
    statusBar()->showMessage(QString("当前: %1").arg(QFileInfo(path).fileName()));
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
    const mviewer::domain::Selection roi = m_compareView->currentROI();
    const QStringList compared = m_compareView->comparedImages();
    // M12.2 (review fix): persist the explicit compared-image list so a compare
    // session with neither ROI nor analysis still reopens correctly.
    for (const QString &cpath : compared)
        ws.comparedImages.push_back(cpath.toStdString());
    // M15: persist the full compare-session snapshot (sync mode, zoom/pan, ROI)
    // so reopening restores the entire compare view, not just the image list.
    if (m_compareView && m_compareView->compareSession().isValid())
        ws.compareSessionJson =
            mviewer::core::serializeCompareSession(m_compareView->compareSession());
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
    mviewer::domain::Workspace ws;
    if (!mviewer::core::deserializeWorkspace(std::string(data.constData(), data.size()), ws) ||
        ws.empty())
    {
        QMessageBox::critical(this, "打开工作区", "工作区文件无效或为空。");
        return;
    }

    // Restore the browsing view: load the workspace root back into the gallery.
    const QString root = QString::fromStdString(ws.rootPath);
    m_currentDir = root;
    m_cachedImagePaths.clear();
    m_dirListDirty = true;
    m_thumbnailPanel->setDirectory(root);

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
    mviewer::domain::CompareSession restoredSession;
    bool haveSession =
        !ws.compareSessionJson.empty() &&
        mviewer::core::deserializeCompareSession(ws.compareSessionJson, restoredSession);
    if (!comparePaths.isEmpty())
    {
        openCompare(comparePaths); // creates m_compareView + setImages + show
        // openCompare() shows the dialog; restore the saved transform snapshot.
        if (haveSession && m_compareView)
            m_compareView->applySession(restoredSession);
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
                });
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction("(无)")->setEnabled(false);
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
            const QString dir = m_appState.lastDir;
            if (dir.isEmpty() || !QDir(dir).exists())
                return;
            m_currentDir = dir;
            m_cachedImagePaths.clear();
            m_dirListDirty = true;
            m_thumbnailPanel->setDirectory(dir);

            const QString img = m_appState.lastImage;
            if (!img.isEmpty() && QFile::exists(img))
            {
                pushHistory(img);
                m_currentImagePath = img;
                m_imageViewer->setImage(img);  // async; imageReady() feeds AnalysisPanel
                m_previewPanel->setImage(img); // async; off UI thread
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
    m_appState.save();

    // Persist the recent-folders LRU alongside app state.
    const QString recentPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recent.json";
    QFile rf(recentPath);
    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        rf.write(QByteArray::fromStdString(m_recent.serialize()));

    // M13.5: persist window geometry/layout (QSettings, independent of workspace).
    {
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
    }

    QMainWindow::closeEvent(event);
}
