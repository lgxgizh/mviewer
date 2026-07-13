#include "mainwindow.h"

#include "core/command/OpenDirectoryCommand.h"
#include "core/command/CompareCommand.h"
#include "core/command/RenameCommand.h"
#include "core/command/DeleteCommand.h"
#include "core/command/ToggleHistogramCommand.h"
#include "exportcommand.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "analysispanel.h"
#include "application/OpenDirectoryUseCase.h"
#include "compareworkspace.h"
#include "core/EventBus.h"
#include "directorytree.h"
#include "imageviewer.h"
#include "previewpanel.h"
#include "thumbnailpanel.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupCommands();
    setWindowTitle("MViewer");
    resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    auto *menuBar = new QMenuBar(this);

    // ----- 文件(&F) -----
    auto *fileMenu = menuBar->addMenu("文件(&F)");
    m_actOpenDir = new QAction("打开目录(&O)", this);
    m_actExit = new QAction("退出(&Q)", this);
    fileMenu->addAction(m_actOpenDir);
    fileMenu->addAction(m_actExit);

    // ----- 视图(&V) -----
    auto *viewMenu = menuBar->addMenu("视图(&V)");
    m_actCompare = new QAction("比较模式(&C)", this);
    m_actToggleAnalysis = new QAction("直方图(&H)", this);
    m_actToggleAnalysis->setCheckable(true);
    m_actToggleAnalysis->setChecked(true);
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
    sortLayout->addStretch(1);
    rightLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(rightWidget);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // ----- Analysis panel (rightmost, smallest stretch) -----
    m_analysisPanel = new AnalysisPanel(this);

    // ----- 3-way horizontal split: left | gallery | analysis -----
    auto *centralSplitter = new QSplitter(Qt::Horizontal, this);
    centralSplitter->addWidget(leftWidget);
    centralSplitter->addWidget(rightWidget);
    centralSplitter->addWidget(m_analysisPanel);
    centralSplitter->setStretchFactor(0, 0);
    centralSplitter->setStretchFactor(1, 1);
    centralSplitter->setStretchFactor(2, 0);
    centralSplitter->setSizes({360, 900, 320});
    setCentralWidget(centralSplitter);

    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle("图片查看 - MViewer");

    // ----- Signals -----
    connect(m_directoryTree, &DirectoryTree::directoryChanged,
            m_thumbnailPanel, &ThumbnailPanel::setDirectory);
    connect(m_directoryTree, &DirectoryTree::directoryChanged, this,
            [this](const QString &path) {
                m_currentDir = path;
                const int n = static_cast<int>(
                    OpenDirectoryUseCase::execute(path.toStdString()).imagePaths.size());
                statusBar()->showMessage(
                    QString("目录: %1, 图片数: %2").arg(path).arg(n));
            });

    connect(m_thumbnailPanel, &ThumbnailPanel::itemClicked, this,
            [this](const QString &path) {
                m_previewPanel->setImage(path);
                QImage img(path);
                m_analysisPanel->setImage(img);
                statusBar()->showMessage(
                    QString("当前: %1, 尺寸: %2x%3")
                        .arg(QFileInfo(path).fileName())
                        .arg(img.width()).arg(img.height()));
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::itemDoubleClicked, this,
            [this](const QString &path) { onImageOpen(path); });
    connect(m_thumbnailPanel, &ThumbnailPanel::compareRequested, this,
            &MainWindow::openCompare);

    // EventBus (decoupled, dual-mode) subscriptions.
    EventBus::instance().subscribe("image.open",
            [this](void *ctx) {
                auto *path = static_cast<QString *>(ctx);
                if (path)
                    onImageOpen(*path);
            });
    EventBus::instance().subscribe("compare.requested",
            [this](void *ctx) {
                auto *paths = static_cast<QStringList *>(ctx);
                if (paths)
                    openCompare(*paths);
            });

    connect(m_imageViewer, &ImageViewer::regionStats,
            m_analysisPanel, &AnalysisPanel::setRegionStats);
    connect(m_imageViewer, &ImageViewer::selectionChanged,
            m_analysisPanel, [this](const QRect &sel) {
                if (sel.isEmpty()) return;
                mviewer::domain::Selection roi;
                roi.x = sel.x(); roi.y = sel.y();
                roi.width = sel.width(); roi.height = sel.height();
                m_analysisPanel->setROI(roi);
            });
    connect(m_imageViewer, &ImageViewer::requestPrev,
            this, [this]() { navigate(-1); });
    connect(m_imageViewer, &ImageViewer::requestNext,
            this, [this]() { navigate(1); });

    connect(sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, sortCombo](int) {
                m_thumbnailPanel->setSortMode(
                    static_cast<ThumbnailPanel::SortMode>(
                        sortCombo->currentData().toInt()));
            });

    // ----- Menu actions -----
    connect(m_actOpenDir, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "打开目录");
        if (!dir.isEmpty()) {
            m_currentDir = dir;
            m_thumbnailPanel->setDirectory(dir);
        }
    });
    connect(m_actExit, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_actCompare, &QAction::triggered, this, [this]() {
        QStringList imgs;
        for (const auto &p :
             OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
            imgs.append(QString::fromStdString(p));
        if (imgs.size() > 8)
            imgs = imgs.mid(0, 8);
        openCompare(imgs);
    });
    connect(m_actToggleAnalysis, &QAction::triggered,
            m_analysisPanel, &QWidget::setVisible);
    connect(m_actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于 MViewer",
                           "MViewer\n\n一个简单的图片查看与分析工具。");
    });

    statusBar()->showMessage("就绪");
}

void MainWindow::setupCommands()
{
    auto &reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<OpenDirectoryCommand>(
        [this]() { m_actOpenDir->trigger(); }));
    reg.registerCommand(std::make_unique<CompareCommand>(
        [this]() { openCompare(); }));
    reg.registerCommand(std::make_unique<RenameCommand>(
        [this]() { m_thumbnailPanel->renameSelected(); }));
    reg.registerCommand(std::make_unique<DeleteCommand>(
        [this]() { m_thumbnailPanel->moveToTrashSelected(); }));
    reg.registerCommand(std::make_unique<ToggleHistogramCommand>(
        [this]() { m_actToggleAnalysis->trigger(); }));
    reg.registerCommand(std::make_unique<ExportCommand>(
        this));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    ICommand *cmd = CommandRegistry::instance().findByShortcut(
        event->key(), static_cast<int>(event->modifiers()));
    if (cmd) {
        cmd->execute();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::onImageOpen(const QString &path)
{
    m_previewPanel->setImage(path);
    m_imageViewer->setImage(path);
    QImage img(path);
    m_analysisPanel->setImage(img);
    m_currentImagePath = path;
    statusBar()->showMessage(
        QString("当前: %1, 尺寸: %2x%3")
            .arg(QFileInfo(path).fileName())
            .arg(img.width()).arg(img.height()));
    if (m_imageViewer->isHidden())
        m_imageViewer->show();
    m_imageViewer->raise();
    m_imageViewer->activateWindow();
}

void MainWindow::openCompare(const QStringList &images)
{
    QStringList imgs = images;
    if (imgs.isEmpty()) {
        if (m_currentDir.isEmpty())
            return;
        for (const auto &p :
             OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
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

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::navigate(int delta)
{
    if (m_currentDir.isEmpty() || m_currentImagePath.isEmpty())
        return;

    QStringList list;
    for (const auto &p :
         OpenDirectoryUseCase::execute(m_currentDir.toStdString()).imagePaths)
        list.append(QString::fromStdString(p));
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
    m_imageViewer->setImage(path);
    m_analysisPanel->setImage(QImage(path));
}
