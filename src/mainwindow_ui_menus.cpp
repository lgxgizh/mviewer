// MainWindow menu construction (M20 P0#1).
#include "mainwindow_p.h"

#include "core/image/ImageFileRotate.h"
#include "core/image/ImageLoadingFacade.h"
#include "display/DisplayColorContextProvider.h"
#include "thumbnailprovider.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QIcon>
#include <QMenuBar>
#include <QProgressDialog>
#include <QStatusBar>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include <cstdint>
#include <memory>
#include <string>

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

    buildEditTransformActions(editMenu);

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
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->selectAll();
            });
    connect(m_actDeselectAll, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->clearSelection();
            });
    connect(m_actInvertSelection, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->invertSelection();
            });
}

void MainWindow::buildEditTransformActions(QMenu *editMenu)
{
    editMenu->addSeparator();
    m_actRotateCW = new QAction(tr("顺时针旋转 90°(&R)"), this);
    m_actRotateCW->setObjectName("rotateCWAction");
    m_actRotateCW->setShortcut(QKeySequence("Ctrl+R"));
    m_actRotateCW->setToolTip(tr("顺时针旋转 90° 并覆盖原文件 (Ctrl+R)"));
    m_actRotateCW->setEnabled(false);
    m_actRotateCCW = new QAction(tr("逆时针旋转 90°(&L)"), this);
    m_actRotateCCW->setObjectName("rotateCCWAction");
    m_actRotateCCW->setShortcut(QKeySequence("Ctrl+Shift+R"));
    m_actRotateCCW->setToolTip(tr("逆时针旋转 90° 并覆盖原文件 (Ctrl+Shift+R)"));
    m_actRotateCCW->setEnabled(false);
    editMenu->addAction(m_actRotateCW);
    editMenu->addAction(m_actRotateCCW);
    connect(m_actRotateCW, &QAction::triggered, this, [this]() { rotateCurrentImage(90); });
    connect(m_actRotateCCW, &QAction::triggered, this, [this]() { rotateCurrentImage(-90); });
    m_actFlipH = new QAction(tr("水平翻转(&H)"), this);
    m_actFlipH->setObjectName("flipHAction");
    m_actFlipH->setShortcut(QKeySequence("Ctrl+Shift+H"));
    m_actFlipH->setToolTip(tr("水平翻转并覆盖原文件 (Ctrl+Shift+H)"));
    m_actFlipH->setEnabled(false);
    m_actFlipV = new QAction(tr("垂直翻转(&V)"), this);
    m_actFlipV->setObjectName("flipVAction");
    m_actFlipV->setShortcut(QKeySequence("Ctrl+Shift+V"));
    m_actFlipV->setToolTip(tr("垂直翻转并覆盖原文件 (Ctrl+Shift+V)"));
    m_actFlipV->setEnabled(false);
    editMenu->addAction(m_actFlipH);
    editMenu->addAction(m_actFlipV);
    connect(m_actFlipH, &QAction::triggered, this, [this]() { flipCurrentImage(true); });
    connect(m_actFlipV, &QAction::triggered, this, [this]() { flipCurrentImage(false); });
}

namespace
{

enum class FileTransformKind : std::uint8_t
{
    Rotate,
    Flip
};

struct FileTransformBatch
{
    int successCount = 0;
    bool canceled = false;
    QStringList failedPaths;
    QStringList failedErrors;
};

// setValue() pumps events only after shownOnce. forceShow() is protected and
// latches that flag; calling it after construction makes Cancel work between
// files. The extra processEvents pass excludes user input so offscreen tests
// do not re-enter QProgressDialog's private cancel-button connection.
class ShownProgressDialog final : public QProgressDialog
{
  public:
    using QProgressDialog::QProgressDialog;
    void reveal()
    {
        forceShow();
    }
};

class BatchFileProgress
{
  public:
    BatchFileProgress(QWidget *parent, int total, FileTransformKind kind)
        : m_total(total), m_kind(kind)
    {
        if (total < 2 || parent == nullptr)
            return;
        const QString label = kind == FileTransformKind::Rotate ? MainWindow::tr("正在旋转…")
                                                                : MainWindow::tr("正在翻转…");
        m_dialog =
            std::make_unique<ShownProgressDialog>(label, QStringLiteral("取消"), 0, total, parent);
        m_dialog->setObjectName(QStringLiteral("batchRotateFlipProgress"));
        m_dialog->setWindowModality(Qt::WindowModal);
        m_dialog->setAutoClose(false);
        m_dialog->setAutoReset(false);
        m_dialog->setMinimumDuration(0);
        m_dialog->setValue(0);
        m_dialog->reveal();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    ~BatchFileProgress()
    {
        dismiss();
    }
    BatchFileProgress(const BatchFileProgress &) = delete;
    BatchFileProgress &operator=(const BatchFileProgress &) = delete;

    bool wasCanceled() const
    {
        return m_dialog && m_dialog->wasCanceled();
    }

    void advance(int finished)
    {
        if (!m_dialog || m_dialog->wasCanceled())
            return;
        m_dialog->setLabelText(finishedLabel(finished));
        m_dialog->setValue(finished);
        if (!m_dialog || m_dialog->wasCanceled())
            return;
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    void dismiss()
    {
        if (!m_dialog)
            return;
        m_dialog->hide();
        m_dialog.reset();
    }

  private:
    QString finishedLabel(int finished) const
    {
        return (m_kind == FileTransformKind::Rotate)
                   ? MainWindow::tr("已旋转 %1 / %2").arg(finished).arg(m_total)
                   : MainWindow::tr("已翻转 %1 / %2").arg(finished).arg(m_total);
    }

    std::unique_ptr<ShownProgressDialog> m_dialog;
    int m_total = 0;
    FileTransformKind m_kind = FileTransformKind::Rotate;
};

QStringList transformTargetPaths(SelectionModel *selection, ThumbnailPanel *panel,
                                 const QString &currentPath)
{
    if (selection && !selection->selection().isEmpty())
        return selection->selection();
    if (panel && !panel->selectedPaths().isEmpty())
        return panel->selectedPaths();
    QStringList paths;
    if (!currentPath.isEmpty())
        paths.append(currentPath);
    return paths;
}

void releaseSourceForRewrite(ImageViewer *viewer, PreviewPanel *preview, const QString &path,
                             const std::string &utf8)
{
    if (viewer)
        viewer->releaseSourceHandles(path);
    if (preview)
        preview->releaseSourceHandles(path);
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
}

void noteTransformSuccess(ThumbnailPanel *panel, const QString &path, const std::string &utf8)
{
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
    if (panel)
        panel->invalidateSourceImage(path);
}

void refreshOpenImage(ImageViewer *viewer, PreviewPanel *preview, MetadataPanel *metadata,
                      const QString &path)
{
    if (path.isEmpty())
        return;
    if (preview)
        preview->setImage(path);
    if (metadata)
        metadata->setImage(path);
    if (viewer && !viewer->isHidden() && viewer->currentPath() == path)
        viewer->refreshSource(path);
}

void restoreMultiSelection(ThumbnailPanel *panel, SelectionModel *selection,
                           const QStringList &paths, const QString &current)
{
    if (!panel || paths.size() <= 1)
        return;
    const QString focus = (!current.isEmpty() && paths.contains(current)) ? current : paths.first();
    panel->selectPaths(paths, focus);
    if (selection)
        selection->setSelection(paths, focus);
}

inline void showStatus(QStatusBar *bar, const QString &text, int ms)
{
    if (bar)
        bar->showMessage(text, ms);
}

QStringList transformFailureLines(const FileTransformBatch &batch)
{
    QStringList lines;
    const int count = static_cast<int>(batch.failedPaths.size());
    for (int i = 0; i < count; ++i)
    {
        lines.append(QStringLiteral("%1: %2").arg(QFileInfo(batch.failedPaths.at(i)).fileName(),
                                                  batch.failedErrors.at(i)));
    }
    return lines;
}

template <typename Fn>
FileTransformBatch runTransformBatch(QWidget *parent, ImageViewer *viewer, PreviewPanel *preview,
                                     ThumbnailPanel *panel, const QStringList &paths,
                                     FileTransformKind kind, Fn &&transform)
{
    BatchFileProgress progress(parent, static_cast<int>(paths.size()), kind);
    FileTransformBatch batch;
    const int total = static_cast<int>(paths.size());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int i = 0; i < total; ++i)
    {
        if (progress.wasCanceled())
            break;
        const QString &path = paths.at(i);
        const std::string utf8 = path.toUtf8().toStdString();
        releaseSourceForRewrite(viewer, preview, path, utf8);
        auto future = QtConcurrent::run([&transform, utf8]() { return transform(utf8); });
        while (!future.isFinished())
        {
            if (progress.wasCanceled())
                break;
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
            QThread::msleep(5);
        }
        if (!future.isFinished())
            future.waitForFinished();
        const auto result = future.result();
        if (!result.ok)
        {
            batch.failedPaths.append(path);
            batch.failedErrors.append(ImageViewer::rotateFailureUserMessage(result));
        }
        else
        {
            noteTransformSuccess(panel, path, utf8);
            ++batch.successCount;
        }
        progress.advance(i + 1);
    }
    QApplication::restoreOverrideCursor();
    const int attempted = batch.successCount + static_cast<int>(batch.failedPaths.size());
    // A cancel that arrives while painting the last item has nothing left to skip.
    batch.canceled = progress.wasCanceled() && attempted < total;
    progress.dismiss();
    return batch;
}

qsizetype skippedCount(const QStringList &paths, const FileTransformBatch &batch)
{
    return paths.size() - batch.successCount - batch.failedPaths.size();
}

void reportRotateBatch(QWidget *parent, QStatusBar *bar, const QStringList &paths,
                       const FileTransformBatch &batch, int degrees)
{
    int norm = degrees % 360;
    if (norm < 0)
        norm += 360;
    if (batch.canceled && batch.failedPaths.isEmpty())
    {
        showStatus(
            bar,
            MainWindow::tr("已取消：成功旋转 %1 / %2 张").arg(batch.successCount).arg(paths.size()),
            3000);
        return;
    }
    if (batch.failedPaths.isEmpty())
    {
        if (paths.size() == 1)
            showStatus(bar, MainWindow::tr("已旋转并覆盖原文件 (%1°)").arg(norm), 2000);
        else
            showStatus(
                bar,
                MainWindow::tr("已旋转 %1 张图片并覆盖原文件 (%2°)").arg(paths.size()).arg(norm),
                3000);
        return;
    }

    const QStringList lines = transformFailureLines(batch);
    if (paths.size() == 1)
    {
        QMessageBox::warning(parent, MainWindow::tr("旋转失败"),
                             MainWindow::tr("无法旋转图片：%1\n%2")
                                 .arg(batch.failedPaths.first(), batch.failedErrors.first()));
    }
    else
    {
        const QString title = batch.canceled ? MainWindow::tr("批量旋转已取消（含失败）")
                                             : MainWindow::tr("批量旋转完成（含失败）");
        const QString msg = MainWindow::tr("成功旋转 %1 张图片，%2 张失败：\n%3")
                                .arg(batch.successCount)
                                .arg(batch.failedPaths.size())
                                .arg(lines.join(QStringLiteral("\n")));
        QMessageBox::warning(parent, title, msg);
    }
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("旋转已取消：%1 成功，%2 失败，%3 未处理")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size())
                       .arg(skippedCount(paths, batch)),
                   3000);
    }
    else
    {
        showStatus(bar,
                   MainWindow::tr("旋转完成：%1 成功，%2 失败")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size()),
                   3000);
    }
}

void reportFlipBatch(QWidget *parent, QStatusBar *bar, const QStringList &paths,
                     const FileTransformBatch &batch, bool horizontal)
{
    if (batch.canceled && batch.failedPaths.isEmpty())
    {
        showStatus(
            bar,
            MainWindow::tr("已取消：成功翻转 %1 / %2 张").arg(batch.successCount).arg(paths.size()),
            3000);
        return;
    }
    if (batch.failedPaths.isEmpty())
    {
        if (paths.size() == 1)
        {
            showStatus(bar,
                       horizontal ? MainWindow::tr("已水平翻转并覆盖原文件")
                                  : MainWindow::tr("已垂直翻转并覆盖原文件"),
                       2000);
        }
        else
        {
            showStatus(bar,
                       horizontal
                           ? MainWindow::tr("已水平翻转 %1 张图片并覆盖原文件").arg(paths.size())
                           : MainWindow::tr("已垂直翻转 %1 张图片并覆盖原文件").arg(paths.size()),
                       3000);
        }
        return;
    }

    const QStringList lines = transformFailureLines(batch);
    if (paths.size() == 1)
    {
        QMessageBox::warning(parent, MainWindow::tr("翻转失败"),
                             MainWindow::tr("无法翻转图片：%1\n%2")
                                 .arg(batch.failedPaths.first(), batch.failedErrors.first()));
    }
    else
    {
        const QString title = batch.canceled ? MainWindow::tr("批量翻转已取消（含失败）")
                                             : MainWindow::tr("批量翻转完成（含失败）");
        const QString msg = MainWindow::tr("成功翻转 %1 张图片，%2 张失败：\n%3")
                                .arg(batch.successCount)
                                .arg(batch.failedPaths.size())
                                .arg(lines.join(QStringLiteral("\n")));
        QMessageBox::warning(parent, title, msg);
    }
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("翻转已取消：%1 成功，%2 失败，%3 未处理")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size())
                       .arg(skippedCount(paths, batch)),
                   3000);
    }
    else
    {
        showStatus(bar,
                   MainWindow::tr("翻转完成：%1 成功，%2 失败")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size()),
                   3000);
    }
}

} // namespace

void MainWindow::rotateCurrentImage(int degrees)
{
    if (m_compareView && m_compareView->isVisible())
    {
        m_compareView->rotateCurrentCell(degrees);
        return;
    }

    const QStringList paths =
        transformTargetPaths(m_selection, m_thumbnailPanel, currentImagePath());
    if (paths.isEmpty())
    {
        showStatus(statusBar(), tr("没有可旋转的图片"), 2000);
        return;
    }

    const FileTransformBatch batch =
        runTransformBatch(this, m_imageViewer, m_previewPanel, m_thumbnailPanel, paths,
                          FileTransformKind::Rotate, [degrees](const std::string &utf8)
                          { return mviewer::core::rotateImageFile(utf8, degrees); });

    const QString cur = currentImagePath();
    if (!cur.isEmpty() && paths.contains(cur))
        refreshOpenImage(m_imageViewer, m_previewPanel, m_metadataPanel, cur);
    reportRotateBatch(this, statusBar(), paths, batch, degrees);
    // Keep the pre-rotate multi-selection after thumbnail invalidate / rebuild.
    restoreMultiSelection(m_thumbnailPanel, m_selection, paths, cur);
}

void MainWindow::flipCurrentImage(bool horizontal)
{
    if (m_compareView && m_compareView->isVisible())
    {
        m_compareView->flipCurrentCell(horizontal);
        return;
    }

    const QStringList paths =
        transformTargetPaths(m_selection, m_thumbnailPanel, currentImagePath());
    if (paths.isEmpty())
    {
        showStatus(statusBar(), tr("没有可翻转的图片"), 2000);
        return;
    }

    const FileTransformBatch batch =
        runTransformBatch(this, m_imageViewer, m_previewPanel, m_thumbnailPanel, paths,
                          FileTransformKind::Flip, [horizontal](const std::string &utf8)
                          { return mviewer::core::flipImageFile(utf8, horizontal); });

    const QString cur = currentImagePath();
    if (!cur.isEmpty() && paths.contains(cur))
        refreshOpenImage(m_imageViewer, m_previewPanel, m_metadataPanel, cur);
    reportFlipBatch(this, statusBar(), paths, batch, horizontal);
    restoreMultiSelection(m_thumbnailPanel, m_selection, paths, cur);
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
    m_actZoomFit = new QAction("适应窗口(&F)", this);
    m_actZoomFit->setObjectName("zoomFitAction");
    m_actZoomFit->setShortcut(QKeySequence("Ctrl+0"));
    m_actZoomActual = new QAction("实际大小(&A)", this);
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
    actBatchAnalyze->setShortcut(QKeySequence("Ctrl+Alt+A"));
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
