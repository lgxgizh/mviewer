// MainWindow command registration and keyboard dispatch (M20 P0#1).
#include "mainwindow_p.h"
#include "runtime_storage.h"

namespace
{
struct ClipboardPasteState
{
    std::atomic<bool> saved{false};
    std::atomic<bool> cancelled{false};
};
} // namespace

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
        "file_delete", "删除到 MViewer 回收站 (Delete)",
        [this]() { m_thumbnailPanel->moveToTrashSelected(); },
        std::vector<CommandShortcut>{{Qt::Key_Delete, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_copy", "复制到...", [this]() { m_thumbnailPanel->copySelectedTo(); },
        // No shortcut: global Ctrl+C copies the image to the clipboard
        // (handled before registry dispatch); binding it here too would be dead.
        std::vector<CommandShortcut>{}));
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
    if (handleWindowKey(event) || handleMetadataKey(event) || handleViewModeKey(event) ||
        handleClipboardKey(event) || handleViewerKey(event))
        return;
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

bool MainWindow::handleWindowKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // Return is a text-editing/navigation key when an editor owns focus. Do
    // not let the window-level quick-preview command open the previous image
    // while the user is committing a path, search query, or text field.
    if (!mod && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
        QWidget *focus = QApplication::focusWidget();
        if (focus && (focus->inherits("QLineEdit") || focus->inherits("QTextEdit") ||
                      focus->inherits("QPlainTextEdit")))
        {
            event->accept();
            return true;
        }
    }
    // M56: F5 refreshes the tree and asks the active-directory monitor for an
    // incremental reconcile; it never re-enters directory navigation.
    if (event->key() == Qt::Key_F5 && !mod)
    {
        m_directoryTree->refresh();
        if (m_directoryMonitor)
            m_directoryMonitor->reconcileNow();
        event->accept();
        return true;
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
            return true;
        }
    }
    // ESC exits fullscreen when the main window itself is fullscreen.
    if (event->key() == Qt::Key_Escape && !mod && isFullScreen())
    {
        showNormal();
        event->accept();
        return true;
    }
    // P1-8: F1 shows the keyboard-shortcut cheat sheet.
    if (event->key() == Qt::Key_F1 && !mod)
    {
        showShortcutsHelp();
        event->accept();
        return true;
    }
    // P1-8: Home/End jump to the first/last image; PageUp/PageDown jump a page.
    if (!mod && (event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
                 event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown))
    {
        navigatePage(event->key());
        event->accept();
        return true;
    }
    return false;
}

bool MainWindow::handleMetadataKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
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
            return true;
        }
    }
    if ((mod & Qt::ControlModifier) && (mod & Qt::ShiftModifier))
    {
        if (event->key() == Qt::Key_P)
        {
            toggleCurrentPick();
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_X)
        {
            toggleCurrentReject();
            event->accept();
            return true;
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
        return true;
    }
    // P0-3 / P1-4 / M19: 'I' or 'M' toggles the metadata overlay (I is the
    // product-facing shortcut; M kept for muscle memory).
    if ((event->key() == Qt::Key_I || event->key() == Qt::Key_M) && !mod)
    {
        toggleMetadataOverlay();
        event->accept();
        return true;
    }
    return false;
}

bool MainWindow::handleViewModeKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // P0-2 / P1-4: view-mode shortcuts.
    if (event->key() == Qt::Key_G && !mod)
    {
        m_thumbnailPanel->setViewMode(ThumbnailPanel::Thumbnail);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_D && !mod)
    {
        m_thumbnailPanel->setViewMode(ThumbnailPanel::Details);
        event->accept();
        return true;
    }
    if ((mod & Qt::ControlModifier) && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6)
    {
        static const ThumbnailPanel::ViewMode modes[] = {
            ThumbnailPanel::Thumbnail, ThumbnailPanel::List,      ThumbnailPanel::Details,
            ThumbnailPanel::Filmstrip, ThumbnailPanel::SmallIcon, ThumbnailPanel::Compact};
        m_thumbnailPanel->setViewMode(modes[event->key() - Qt::Key_1]);
        event->accept();
        return true;
    }
    return false;
}

bool MainWindow::handleClipboardKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // P1-4: Ctrl+C copies the current image to clipboard; Ctrl+Shift+C copies its path.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        if ((mod & Qt::ShiftModifier))
        {
            if (!currentImagePath().isEmpty())
            {
                const QString nativePath = QDir::toNativeSeparators(currentImagePath());
                QApplication::clipboard()->setText(nativePath);
                if (statusBar())
                    statusBar()->showMessage(tr("已复制路径: %1").arg(nativePath), 2000);
            }
        }
        else
        {
            copyCurrentImageToClipboard();
            if (statusBar())
                statusBar()->showMessage(tr("已复制图片到剪贴板"), 2000);
        }
        event->accept();
        return true;
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
                startClipboardPaste(img);
            else
                statusBar()->showMessage("剪贴板中无图片数据", 3000);
        }
        else
            statusBar()->showMessage("剪贴板中无图片数据", 3000);
        event->accept();
        return true;
    }
    return false;
}

void MainWindow::startClipboardPaste(const QImage &img)
{
    // Persist to a temp file so ImageViewer can load it via its normal async
    // path (keeps decode/histogram consistent).
    const QString tempRoot = mviewer::runtime::writableDirectory(QStandardPaths::TempLocation);
    if (tempRoot.isEmpty())
    {
        statusBar()->showMessage("无法创建剪贴板临时文件", 3000);
        return;
    }
    const QString tmpDir = QDir(tempRoot).filePath("mviewer-clip-paste");
    if (!QDir().mkpath(tmpDir))
    {
        statusBar()->showMessage("无法创建剪贴板临时目录", 3000);
        return;
    }

    cancelClipboardPaste();
    cleanupClipboardPasteTemps();
    const QString tmpPath = QDir(tmpDir).filePath(
        QStringLiteral("paste_%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const uint64_t generation = ++m_clipboardPasteGeneration;
    const auto alive = m_clipboardPasteAlive = std::make_shared<std::atomic<bool>>(true);
    const QPointer<MainWindow> guard(this);
    const auto state = std::make_shared<ClipboardPasteState>();

    m_clipboardPasteTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::UI,
        [img, tmpPath, state](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
            {
                state->cancelled.store(true, std::memory_order_relaxed);
                return;
            }
            const bool saved = img.save(tmpPath, "PNG");
            if (!saved || ctx.isCancelled())
            {
                state->cancelled.store(ctx.isCancelled(), std::memory_order_relaxed);
                QFile::remove(tmpPath);
                return;
            }
            state->saved.store(true, std::memory_order_release);
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, alive, generation, state, tmpPath]()
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, alive, generation, state, tmpPath]()
                {
                    if (!alive->load(std::memory_order_relaxed) || !guard ||
                        generation != guard->m_clipboardPasteGeneration)
                    {
                        if (state->saved.load(std::memory_order_acquire))
                            QFile::remove(tmpPath);
                        return;
                    }
                    MainWindow *window = guard.data();
                    window->m_clipboardPasteTask.reset();
                    if (state->saved.load(std::memory_order_acquire))
                    {
                        window->m_clipboardPastePath = tmpPath;
                        window->onImageOpen(tmpPath);
                        window->statusBar()->showMessage("已从剪贴板粘贴图片", 3000);
                    }
                    else
                    {
                        QFile::remove(tmpPath);
                        window->statusBar()->showMessage(
                            state->cancelled.load(std::memory_order_relaxed) ? "剪贴板粘贴已取消"
                                                                             : "无法保存剪贴板图片",
                            3000);
                    }
                },
                Qt::QueuedConnection);
        });

    if (!m_clipboardPasteTask)
    {
        alive->store(false, std::memory_order_relaxed);
        QFile::remove(tmpPath);
        statusBar()->showMessage("剪贴板粘贴无法排队", 3000);
    }
}

void MainWindow::cancelClipboardPaste()
{
    ++m_clipboardPasteGeneration;
    if (m_clipboardPasteAlive)
        m_clipboardPasteAlive->store(false, std::memory_order_relaxed);
    if (m_clipboardPasteTask)
        TaskScheduler::cancel(m_clipboardPasteTask);
    m_clipboardPasteTask.reset();
}

void MainWindow::cleanupClipboardPasteTemps()
{
    const QString tempRoot = mviewer::runtime::writableDirectory(QStandardPaths::TempLocation);
    if (tempRoot.isEmpty())
        return;
    const QDir dir(QDir(tempRoot).filePath(QStringLiteral("mviewer-clip-paste")));
    if (!dir.exists())
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    for (const QFileInfo &info :
         dir.entryInfoList(QStringList() << QStringLiteral("paste_*.png"), QDir::Files))
    {
        if (info.absoluteFilePath() != m_clipboardPastePath && info.lastModified() < cutoff)
            QFile::remove(info.absoluteFilePath());
    }
}

bool MainWindow::handleViewerKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // P0-4 / P1-4: Space triggers compare for the current + next image.
    if (event->key() == Qt::Key_Space && !mod)
    {
        openQuickCompare();
        event->accept();
        return true;
    }
    // Compare mode on a plain 'C' or 'P' — P is the advertised gallery
    // shortcut so a multi-select does not require the context menu.
    if (!mod && (event->key() == Qt::Key_C || event->key() == Qt::Key_P))
    {
        if (m_actCompare && m_actCompare->isEnabled())
            m_actCompare->trigger();
        else
            statusBar()->showMessage(tr("需要选择 2-8 张图片才能比较"), 3000);
        event->accept();
        return true;
    }
    // 'S' toggles the slideshow (same plain-key rationale as 'C').
    if (event->key() == Qt::Key_S && !mod)
    {
        toggleSlideshow();
        event->accept();
        return true;
    }
    // Viewer zoom keys: plain +/-/0/1 (forwarded to the viewer when visible).
    if (!mod && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal))
    {
        zoomViewer(0);
        event->accept();
        return true;
    }
    if (!mod && event->key() == Qt::Key_Minus)
    {
        zoomViewer(1);
        event->accept();
        return true;
    }
    if (!mod && event->key() == Qt::Key_0)
    {
        zoomViewer(2);
        event->accept();
        return true;
    }
    if (!mod && event->key() == Qt::Key_1)
    {
        zoomViewer(3);
        event->accept();
        return true;
    }
    return false;
}

QString MainWindow::shortcutsHelpHtml()
{
    // P1-8: a single, authoritative cheat sheet so users never have to guess.
    return QStringLiteral(
        "<style>td{padding:2px 14px 2px 0;} th{text-align:left;padding-top:8px;}"
        "kbd{background:#333;color:#fff;border-radius:3px;padding:1px 5px;}</style>"
        "<table>"
        "<tr><th colspan='2'>文件与导航</th></tr>"
        "<tr><td><kbd>Ctrl+O</kbd> / <kbd>Ctrl+Shift+O</kbd></td><td>打开目录 / 打开文件</td></tr>"
        "<tr><td><kbd>Ctrl+L</kbd> / <kbd>Alt+D</kbd></td><td>聚焦地址栏（快速定位路径）</td></tr>"
        "<tr><td><kbd>Alt+↑</kbd></td><td>返回上一级目录</td></tr>"
        "<tr><td><kbd>Ctrl+V</kbd></td><td>从剪贴板粘贴图片（截图后直接查看）</td></tr>"
        "<tr><td><kbd>Ctrl+D</kbd></td><td>收藏当前目录</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+F</kbd></td><td>全局搜索</td></tr>"
        "<tr><td><kbd>Ctrl+F</kbd></td><td>聚焦目录树过滤框（快速查找文件夹）</td></tr>"
        "<tr><td><kbd>F1</kbd></td><td>快捷键帮助</td></tr>"
        "<tr><td><kbd>Ctrl+Q</kbd></td><td>退出</td></tr>"
        "<tr><th colspan='2'>浏览与选择</th></tr>"
        "<tr><td><kbd>←</kbd> / <kbd>→</kbd> / 鼠标侧键</td><td>上一张 / 下一张（循环）</td></tr>"
        "<tr><td><kbd>Alt+←</kbd> / <kbd>Alt+→</kbd></td><td>历史导航：上一步 / 下一步</td></tr>"
        "<tr><td><kbd>Enter</kbd></td><td>在查看器中打开选中图片</td></tr>"
        "<tr><td><kbd>Ctrl+A</kbd> / <kbd>Ctrl+Shift+A</kbd></td><td>全选 / "
        "取消全选画廊图片</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+I</kbd></td><td>反选画廊图片</td></tr>"
        "<tr><td><kbd>Home</kbd> / <kbd>End</kbd></td><td>第一张 / "
        "最后一张（查看器中同样有效）</td></tr>"
        "<tr><td><kbd>PageUp</kbd> / <kbd>PageDown</kbd></td><td>上翻 / 下翻一页（10 "
        "张，查看器中同样有效）</td></tr>"
        "<tr><td><kbd>F5</kbd></td><td>刷新目录树与画廊</td></tr>"
        "<tr><td><kbd>Ctrl+滚轮</kbd> / <kbd>Ctrl++</kbd> / "
        "<kbd>Ctrl+-</kbd></td><td>调整缩略图大小（<kbd>Ctrl+0</kbd> 重置）</td></tr>"
        "<tr><td><kbd>Tab</kbd></td><td>显示 / 隐藏侧边面板</td></tr>"
        "<tr><th colspan='2'>缩放（查看器）</th></tr>"
        "<tr><td><kbd>+</kbd> / <kbd>-</kbd>（或 <kbd>Ctrl++</kbd> / <kbd>Ctrl+-</kbd>）</td><td>"
        "放大 / 缩小</td></tr>"
        "<tr><td><kbd>0</kbd> / <kbd>1</kbd> / <kbd>2</kbd></td><td>适应窗口 / 实际大小(100%) / "
        "200%</td></tr>"
        "<tr><td><kbd>F</kbd></td><td>适应窗口</td></tr>"
        "<tr><td>双击</td><td>适应窗口 ↔ 100% 切换</td></tr>"
        "<tr><td><kbd>F11</kbd></td><td>全屏切换</td></tr>"
        "<tr><td><kbd>S</kbd></td><td>幻灯片放映（3 秒/张，循环）</td></tr>"
        "<tr><td><kbd>ESC</kbd></td><td>退出全屏 / 关闭查看器 / 停止放映 / 关闭信息浮层</td></tr>"
        "<tr><th colspan='2'>视图模式</th></tr>"
        "<tr><td><kbd>G</kbd></td><td>缩略图视图</td></tr>"
        "<tr><td><kbd>D</kbd></td><td>详情视图</td></tr>"
        "<tr><td><kbd>Ctrl+1</kbd>…<kbd>Ctrl+4</kbd></td><td>缩略图 / 列表 / 详情 / "
        "胶片条</td></tr>"
        "<tr><td><kbd>Ctrl+5</kbd> / <kbd>Ctrl+6</kbd></td><td>小图标 / 紧凑</td></tr>"
        "<tr><th colspan='2'>比较（仅比较窗口）</th></tr>"
        "<tr><td><kbd>P</kbd> / <kbd>C</kbd>（浏览窗口）</td><td>选中 2–8 张后打开比较</td></tr>"
        "<tr><td><kbd>Space</kbd></td><td>浏览：快速比较当前选中；比较窗口：按住临时切换</td></tr>"
        "<tr><td><kbd>B</kbd> / <kbd>S</kbd> / <kbd>W</kbd> / <kbd>O</kbd></td>"
        "<td>闪烁 / 分割 / 滑动 / 叠加</td></tr>"
        "<tr><td><kbd>H</kbd></td><td>Diff 高亮（比较窗口；浏览窗口请用 Alt+H 打开分析）</td></tr>"
        "<tr><td><kbd>Z</kbd> / <kbd>D</kbd></td><td>同步缩放 / 同步拖动</td></tr>"
        "<tr><td><kbd>R</kbd> / <kbd>L</kbd> / <kbd>I</kbd></td><td>准星 / 像素连线 / "
        "侧栏</td></tr>"
        "<tr><td><kbd>1</kbd>~<kbd>8</kbd></td><td>N 联布局预设（比较 N 张）</td></tr>"
        "<tr><td><kbd>P</kbd> / <kbd>N</kbd></td><td>比较下一对 / 上一对（连续对比）</td></tr>"
        "<tr><td><kbd>Shift+0</kbd></td><td>重置通道叠加（还原完整 RGB）</td></tr>"
        "<tr><td><kbd>PgUp</kbd>/<kbd>PgDn</kbd> / <kbd>←</kbd>/<kbd>→</kbd></td>"
        "<td>连续导航（保留模式）</td></tr>"
        "<tr><td><kbd>F</kbd> / <kbd>X</kbd> / <kbd>?</kbd></td><td>Fit / 交换 A/B / 帮助</td></tr>"
        "<tr><td><kbd>ESC</kbd></td><td>有选区则清除选区，再按一次退出比较</td></tr>"
        "<tr><th colspan='2'>分析 / 信息</th></tr>"
        "<tr><td><kbd>Alt+H</kbd></td><td>分析面板（浏览窗口）</td></tr>"
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
        "<tr><th colspan='2'>旋转 / 翻转</th></tr>"
        "<tr><td><kbd>Ctrl+R</kbd> / <kbd>Ctrl+Shift+R</kbd></td>"
        "<td>顺/逆时针 90°（浏览覆盖原文件；比较窗口仅预览当前窗格）</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+H</kbd> / <kbd>Ctrl+Shift+V</kbd></td>"
        "<td>水平 / 垂直翻转（浏览覆盖原文件；比较窗口仅预览当前窗格）</td></tr>"
        "<tr><th colspan='2'>文件操作</th></tr>"
        "<tr><td><kbd>F2</kbd></td><td>重命名选中图片</td></tr>"
        "<tr><td><kbd>Delete</kbd></td><td>删除到 MViewer 回收站</td></tr>"
        "<tr><td><kbd>Ctrl+M</kbd></td><td>移动到...</td></tr>"
        "<tr><td><kbd>Ctrl+E</kbd></td><td>在资源管理器中显示</td></tr>"
        "<tr><td><kbd>Ctrl+Shift+B</kbd></td><td>批量处理</td></tr>"
        "</table>");
}

void MainWindow::showShortcutsHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("键盘快捷键"));
    dlg.resize(480, 560);
    auto *lay = new QVBoxLayout(&dlg);
    auto *browser = new QTextBrowser(&dlg);
    browser->setHtml(shortcutsHelpHtml());
    browser->setOpenExternalLinks(false);
    lay->addWidget(browser);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

QString MainWindow::userGuideHtml()
{
    return QStringLiteral(
        "<h2>MViewer 使用说明</h2>"
        "<p>图像算法工程师用的浏览 / 比较 / 分析工具。下面只写日常路径。</p>"
        "<h3>1. 浏览</h3>"
        "<ol>"
        "<li>打开一个图片目录（Ctrl+O）。缩略图会陆续出来。</li>"
        "<li>单击看大图；Ctrl+单击 / Shift+单击多选。</li>"
        "<li>滚轮缩放，双击在适应窗口和 100% 之间切换。</li>"
        "</ol>"
        "<h3>2. 比较（主路径）</h3>"
        "<ol>"
        "<li>在缩略图里选中 <b>2–8 张</b>图。</li>"
        "<li>按 <kbd>P</kbd> 或 <kbd>C</kbd> 进入比较（也可点工具栏「比较」或右键「比较」）。"
        "选不够 2 张时，状态栏会提示原因。</li>"
        "<li>默认同步缩放和拖动。底部状态条显示 <b>PSNR / SSIM</b>。</li>"
        "<li>模式（仅 2 张时）：<kbd>B</kbd> 闪烁 · <kbd>S</kbd> 左右分割 · "
        "<kbd>W</kbd> 滑动 · <kbd>O</kbd> 叠加 · <kbd>K</kbd> "
        "棋盘。叠加/棋盘的滑条用到才出现。</li>"
        "<li>勾选「显示差异」看热力图；「对齐」可在算指标前自动对齐。</li>"
        "<li>在图上<b>右键拖</b>画 ROI。黄框上方标 <b>A / B</b>，右下角统计也是 A 一行、B "
        "一行。</li>"
        "<li><kbd>Esc</kbd>：有选区先清框，再按退出。也可点「退出比较」。</li>"
        "</ol>"
        "<h3>3. 比较窗口常用键</h3>"
        "<table border='1' cellpadding='4' cellspacing='0'>"
        "<tr><td><kbd>Z</kbd> / <kbd>D</kbd></td><td>同步缩放 / 同步拖动</td></tr>"
        "<tr><td><kbd>Space</kbd></td><td>按住：在 A 窗格临时看 B</td></tr>"
        "<tr><td><kbd>H</kbd></td><td>差异高亮</td></tr>"
        "<tr><td><kbd>R</kbd> / <kbd>L</kbd></td><td>同步准星 / 像素连线</td></tr>"
        "<tr><td><kbd>X</kbd></td><td>交换 A/B</td></tr>"
        "<tr><td><kbd>F</kbd></td><td>全部适应窗口</td></tr>"
        "<tr><td><kbd>PgUp</kbd> / <kbd>PgDn</kbd></td><td>上一对 / 下一对</td></tr>"
        "<tr><td><kbd>?</kbd></td><td>底部快捷键提示（不改窗口标题）</td></tr>"
        "<tr><td><kbd>Shift+1</kbd>…<kbd>5</kbd></td><td>通道 RGB / R / G / B / Y</td></tr>"
        "</table>"
        "<h3>4. 浏览窗口常用键</h3>"
        "<table border='1' cellpadding='4' cellspacing='0'>"
        "<tr><td><kbd>P</kbd> / <kbd>C</kbd></td><td>打开比较（需 2–8 张）</td></tr>"
        "<tr><td><kbd>Space</kbd></td><td>当前图与下一张快速比较</td></tr>"
        "<tr><td><kbd>Alt+H</kbd></td><td>分析面板（打开即出直方图）</td></tr>"
        "<tr><td><kbd>I</kbd> / <kbd>M</kbd></td><td>图片信息浮层</td></tr>"
        "<tr><td><kbd>S</kbd></td><td>幻灯片</td></tr>"
        "<tr><td><kbd>Ctrl+R</kbd> / <kbd>Ctrl+Shift+H</kbd></td>"
        "<td>旋转 / 翻转并覆盖原文件</td></tr>"
        "<tr><td><kbd>F1</kbd></td><td>完整快捷键表</td></tr>"
        "</table>"
        "<p>完整快捷键表：菜单「帮助 → 键盘快捷键」或 <kbd>F1</kbd>。</p>");
}

void MainWindow::showUserGuide()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("使用说明"));
    dlg.resize(560, 640);
    auto *lay = new QVBoxLayout(&dlg);
    auto *browser = new QTextBrowser(&dlg);
    browser->setHtml(userGuideHtml());
    browser->setOpenExternalLinks(false);
    lay->addWidget(browser);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}
