// MainWindow command registration and keyboard dispatch (M20 P0#1).
#include "Language.h"
#include "mainwindow_p.h"
#include "runtime_storage.h"
#include "shortcutdigits.h"

#include <QCursor>

#include <QCoreApplication>

namespace
{
struct ClipboardPasteState
{
    std::atomic<bool> saved{false};
    std::atomic<bool> cancelled{false};
};

QString clipboardPasteDirectory()
{
    const QString tempRoot = mviewer::runtime::writableDirectory(QStandardPaths::TempLocation);
    if (tempRoot.isEmpty())
        return {};
    return QDir(tempRoot).filePath(
        QStringLiteral("mviewer-clip-%1").arg(QCoreApplication::applicationPid()));
}
} // namespace

namespace mviewer_keys
{
bool yieldPrintableToFocusedEntry(const QKeyEvent *key);

QWidget *textEntryWidget(QWidget *widget)
{
    for (; widget; widget = widget->parentWidget())
        if (widget->inherits("QLineEdit") || widget->inherits("QTextEdit") ||
            widget->inherits("QPlainTextEdit") || widget->inherits("QAbstractSpinBox"))
            return widget;
    return nullptr;
}
bool textEntryOwnsKey(QWidget *widget, const QKeyEvent *key)
{
    if (!textEntryWidget(widget) || !key)
        return false;
    const auto mods = key->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    // Clipboard shortcuts stay with the editor. Ctrl+Shift+C still copies the
    // image path via MainWindow when the gallery owns the key.
    if (mods == Qt::ControlModifier && (key->key() == Qt::Key_C || key->key() == Qt::Key_V ||
                                        key->key() == Qt::Key_X || key->key() == Qt::Key_A))
        return true;
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return false;
    const int code = key->key();
    return code == Qt::Key_Space || (code >= Qt::Key_Exclam && code <= Qt::Key_AsciiTilde);
}
} // namespace mviewer_keys

void MainWindow::setupCommands()
{
    auto &reg = CommandRegistry::instance();
    reg.registerCommand(
        std::make_unique<OpenDirectoryCommand>([this]() { m_actOpenDir->trigger(); }));
    reg.registerCommand(std::make_unique<CompareCommand>([this]() { openCompare(); }));
    reg.registerCommand(std::make_unique<RenameCommand>(
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->renameSelected();
        }));
    reg.registerCommand(std::make_unique<DeleteCommand>(
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->moveToTrashSelected();
        }));
    reg.registerCommand(
        std::make_unique<ToggleHistogramCommand>([this]() { m_actToggleAnalysis->trigger(); }));
    reg.registerCommand(std::make_unique<ExportCommand>(this));

    // M9: Left/Right navigate, Enter opens the viewer, F11 toggles fullscreen.
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "navigate_prev", "Previous Image (Left)", [this]() { navigate(-1); },
        std::vector<CommandShortcut>{{Qt::Key_Left, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "navigate_next", "Next Image (Right)", [this]() { navigate(1); },
        std::vector<CommandShortcut>{{Qt::Key_Right, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "quick_preview", "Open in Viewer (Enter)",
        [this]()
        {
            if (!currentImagePath().isEmpty())
                onImageOpen(currentImagePath());
        },
        std::vector<CommandShortcut>{{Qt::Key_Return, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "fullscreen", "Fullscreen (F11)", [this]() { toggleFullscreen(); },
        std::vector<CommandShortcut>{{Qt::Key_F11, 0}}));

    // M18: file-management shortcuts for the selected gallery items.
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_rename", "Rename (F2)",
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->renameSelected();
        },
        std::vector<CommandShortcut>{{Qt::Key_F2, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_delete", "Move to Recycle Bin (Delete)",
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->moveToTrashSelected();
        },
        std::vector<CommandShortcut>{{Qt::Key_Delete, 0}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_copy", "Copy To...",
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->copySelectedTo();
        },
        // No shortcut: global Ctrl+C copies the image to the clipboard
        // (handled before registry dispatch); binding it here too would be dead.
        std::vector<CommandShortcut>{}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_move", "Move To... (Ctrl+M)",
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->moveSelectedTo();
        },
        std::vector<CommandShortcut>{{Qt::Key_M, Qt::ControlModifier}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_copy_other_pane", "Copy to Other Pane",
        [this]() { copyActiveSelectionToOtherPane(); },
        std::vector<CommandShortcut>{
            {Qt::Key_C, static_cast<int>(Qt::ControlModifier | Qt::AltModifier)}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_move_other_pane", "Move to Other Pane",
        [this]() { moveActiveSelectionToOtherPane(); },
        std::vector<CommandShortcut>{
            {Qt::Key_M, static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)}}));
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "file_reveal", "Show in File Manager (Ctrl+E)",
        [this]()
        {
            if (ThumbnailPanel *panel = activeThumbnailPanel())
                panel->revealSelected();
        },
        std::vector<CommandShortcut>{{Qt::Key_E, Qt::ControlModifier}}));
    // Ctrl+F focuses the gallery address bar (pathEdit).
    reg.registerCommand(std::make_unique<CallbackCommand>(
        "dir_filter", "Address Bar (Ctrl+F)", [this]() { focusAddressBar(); },
        std::vector<CommandShortcut>{{Qt::Key_F, Qt::ControlModifier}}));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Line edits own typed characters and Ctrl+C; forward those keys back.
    // Unmodified I/M stay here and toggle metadata despite a focused editor.
    if (QWidget *entry = mviewer_keys::textEntryWidget(QApplication::focusWidget()))
    {
        if (mviewer_keys::textEntryOwnsKey(entry, event) && entry != this &&
            mviewer_keys::yieldPrintableToFocusedEntry(event))
        {
            static bool yielding = false;
            if (!yielding)
            {
                yielding = true;
                QApplication::sendEvent(entry, event);
                event->accept();
                yielding = false;
            }
            return;
        }
    }
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
    // Return stays with a focused editor. Do not reopen the previous image
    // while the user is committing a path or search query.
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
    // M56: F5 refreshes the tree and reconciles; it does not re-enter navigation.
    if (event->key() == Qt::Key_F5 && !mod)
    {
        m_directoryTree->refresh();
        if (m_directoryMonitor)
            m_directoryMonitor->reconcileNow();
        event->accept();
        return true;
    }
    // A running slideshow takes the first Esc. Metadata and fullscreen follow.
    if (event->key() == Qt::Key_Escape && !mod && m_slideshowTimer && m_slideshowTimer->isActive())
    {
        stopSlideshow();
        event->accept();
        return true;
    }
    // P0-3 / A-5: ESC dismisses the metadata overlay AND the floating panel.
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
    const int digit = mviewer::ui::shortcutDigit(event);
    if ((mod & Qt::ControlModifier) && (mod & Qt::ShiftModifier) && digit >= 0 && digit <= 5)
    {
        // Windows reports Ctrl+Shift+1 as Key_Exclam; shortcutDigit maps it.
        rateCurrentImage(digit);
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

bool MainWindow::isThumbnailPanelPasteTarget() const
{
    ThumbnailPanel *panel = activeThumbnailPanel();
    if (!panel || panel->currentDir().isEmpty())
        return false;
    // Compare mode owns the canvas; keep the legacy temp-file paste there.
    if (m_compareView && !m_compareView->isHidden())
        return false;
    QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == panel || panel->isAncestorOf(focus)))
        return true;
    if (panel->underMouse())
        return true;
    if (QWidget *viewport = panel->viewport())
    {
        if (viewport->underMouse())
            return true;
        const QPoint local = viewport->mapFromGlobal(QCursor::pos());
        if (viewport->rect().contains(local))
            return true;
    }
    return false;
}

bool MainWindow::handleClipboardKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // Ctrl+C copies the current image; Ctrl+Shift+C copies its path.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        if ((mod & Qt::ShiftModifier))
        {
            if (!currentImagePath().isEmpty())
            {
                const QString nativePath = QDir::toNativeSeparators(currentImagePath());
                QApplication::clipboard()->setText(nativePath);
                if (statusBar())
                    statusBar()->showMessage(tr("Path copied: %1").arg(nativePath), 2000);
            }
        }
        else
        {
            const bool copying = !currentImagePath().isEmpty() && m_imageViewer;
            if (copying)
                copyCurrentImageToClipboard();
            if (statusBar())
                statusBar()->showMessage(copying ? tr("Copying image to clipboard...")
                                                 : tr("No image available to copy"),
                                         copying ? 0 : 2000);
        }
        event->accept();
        return true;
    }
    // Ctrl+V: when the gallery has focus or the pointer is over it, paste into
    // the browsed folder. Otherwise keep the legacy temp-file → viewer path.
    // Ctrl+Alt+V is Flip Vertical, so Alt must not paste.
    if ((mod & Qt::ControlModifier) && event->key() == Qt::Key_V && !(mod & Qt::ShiftModifier) &&
        !(mod & Qt::AltModifier))
    {
        if (mviewer_keys::textEntryWidget(QApplication::focusWidget()))
            return false;
        if (isThumbnailPanelPasteTarget())
        {
            ThumbnailPanel *panel = activeThumbnailPanel();
            const QStringList written =
                panel ? panel->pasteClipboardIntoCurrentDirectory() : QStringList();
            if (written.isEmpty())
            {
                if (statusBar())
                    statusBar()->showMessage(tr("No image data on the clipboard"), 3000);
            }
            else if (statusBar())
            {
                statusBar()->showMessage(
                    written.size() == 1
                        ? tr("Pasted into folder: %1").arg(QFileInfo(written.first()).fileName())
                        : tr("Pasted %1 images into folder").arg(written.size()),
                    3000);
            }
            event->accept();
            return true;
        }
        const QClipboard *cb = QApplication::clipboard();
        const QMimeData *md = cb ? cb->mimeData() : nullptr;
        if (md && md->hasImage())
        {
            const QImage img = qvariant_cast<QImage>(md->imageData());
            if (!img.isNull())
                startClipboardPaste(img);
            else if (statusBar())
                statusBar()->showMessage(tr("No image data on the clipboard"), 3000);
        }
        else if (statusBar())
            statusBar()->showMessage(tr("No image data on the clipboard"), 3000);
        event->accept();
        return true;
    }
    return false;
}

void MainWindow::startClipboardPaste(const QImage &img)
{
    // Persist to a temp file so ImageViewer loads it on its normal async path.
    const QString tmpDir = clipboardPasteDirectory();
    if (tmpDir.isEmpty())
    {
        statusBar()->showMessage(tr("Unable to create a temporary clipboard file"), 3000);
        return;
    }
    if (!QDir().mkpath(tmpDir))
    {
        statusBar()->showMessage(tr("Unable to create a temporary clipboard folder"), 3000);
        return;
    }

    if (!m_clipboardPastePath.isEmpty())
    {
        QFile::remove(m_clipboardPastePath);
        m_clipboardPastePath.clear();
    }
    cancelClipboardPaste();
    cleanupClipboardPasteTemps();
    if (!QDir().mkpath(tmpDir))
    {
        statusBar()->showMessage(tr("Unable to create a temporary clipboard folder"), 3000);
        return;
    }
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
                        window->statusBar()->showMessage(tr("Pasted image from clipboard"), 3000);
                    }
                    else
                    {
                        QFile::remove(tmpPath);
                        window->statusBar()->showMessage(
                            state->cancelled.load(std::memory_order_relaxed)
                                ? tr("Clipboard paste cancelled")
                                : tr("Unable to save the clipboard image"),
                            3000);
                    }
                },
                Qt::QueuedConnection);
        });

    if (!m_clipboardPasteTask)
    {
        alive->store(false, std::memory_order_relaxed);
        QFile::remove(tmpPath);
        statusBar()->showMessage(tr("Clipboard paste could not be queued"), 3000);
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
    const QString dirPath = clipboardPasteDirectory();
    if (dirPath.isEmpty())
        return;
    QDir dir(dirPath);
    if (!dir.exists())
        return;
    const QString keep = QFileInfo(m_clipboardPastePath).absoluteFilePath();
    for (const QFileInfo &info :
         dir.entryInfoList(QStringList() << QStringLiteral("paste_*.png"), QDir::Files))
    {
        if (info.absoluteFilePath() != keep)
            QFile::remove(info.absoluteFilePath());
    }
    if (dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
        QDir().rmdir(dir.absolutePath());
}

bool MainWindow::handleViewerKey(QKeyEvent *event)
{
    const auto mod = event->modifiers();
    // Shift+C / Shift+B belong to ImageViewer (copy hex / RGB). Do not treat
    // them as the plain-key compare or browse shortcuts.
    const auto shiftBare =
        mod & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (shiftBare == Qt::ShiftModifier && (event->key() == Qt::Key_C || event->key() == Qt::Key_B))
    {
        if (m_imageViewer && QApplication::focusWidget() != m_imageViewer)
            QApplication::sendEvent(m_imageViewer, event);
        event->accept();
        return true;
    }
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
            statusBar()->showMessage(tr("Select 2-8 images to compare"), 3000);
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
    if (!mod)
    {
        switch (event->key())
        {
        case Qt::Key_Minus:
            zoomViewer(1);
            event->accept();
            return true;
        case Qt::Key_0:
            zoomViewer(2);
            event->accept();
            return true;
        case Qt::Key_F:
            // Cheat sheet: F fits the window. F11 is fullscreen. Accept F with no
            // image so it cannot fall through to the fullscreen command.
            if (!currentImagePath().isEmpty())
                zoomViewer(2);
            event->accept();
            return true;
        case Qt::Key_1:
            zoomViewer(3);
            event->accept();
            return true;
        }
    }
    if (!mod && event->key() == Qt::Key_2)
    {
        if (!currentImagePath().isEmpty())
            zoomViewer(5);
        event->accept();
        return true;
    }
    return false;
}

QString MainWindow::shortcutsHelpHtml()
{
    // P1-8: a single, authoritative cheat sheet so users never have to guess.
    // The page lives in resources/help/ in every UI language.
    return mviewer::ui::Language::localizedHelpHtml(QStringLiteral("shortcuts"));
}

void MainWindow::showShortcutsHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard Shortcuts"));
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
    return mviewer::ui::Language::localizedHelpHtml(QStringLiteral("guide"));
}

void MainWindow::showUserGuide()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("User Guide"));
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
    case 4:
        m_imageViewer->zoomTo(0.5);
        break;
    case 5:
        m_imageViewer->zoomTo(2.0);
        break;
    case 6:
        m_imageViewer->zoomTo(4.0);
        break;
    case 7:
        m_imageViewer->zoomTo(8.0);
        break;
    }
}

void MainWindow::showZoomPresetMenu(const QPoint &globalPos)
{
    if (m_compareView && !m_compareView->isHidden())
    {
        QMenu menu(this);
        menu.addAction(tr("Fit Window (F)"), this, [this]() { m_compareView->fitAll(); });
        menu.addAction(tr("Actual size 100% (Ctrl+1)"), this,
                       [this]() { m_compareView->zoomActual(); });
        menu.exec(globalPos);
        return;
    }
    if (!m_imageViewer || m_imageViewer->isHidden() || currentImagePath().isEmpty())
        return;
    QMenu menu(this);
    menu.addAction("Fit to window (0 / F)", this, [this]() { m_imageViewer->zoomFit(); });
    menu.addAction("Actual Size 100% (1)", this, [this]() { m_imageViewer->zoomActual(); });
    menu.addSeparator();
    menu.addAction("50%", this, [this]() { m_imageViewer->zoomTo(0.5); });
    menu.addAction("200% (2)", this, [this]() { m_imageViewer->zoomTo(2.0); });
    menu.addAction("400%", this, [this]() { m_imageViewer->zoomTo(4.0); });
    menu.addAction(tr("800% (Pixel Grid)"), this, [this]() { m_imageViewer->zoomTo(8.0); });
    menu.exec(globalPos);
}
