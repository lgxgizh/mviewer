// MainWindow menu construction (M20 P0#1).
#include "mainwindow_p.h"

#include "ShortcutHelper.h"
#include "display/DisplayColorContextProvider.h"
#include "fileopmessages.h"

#include <QMenuBar>

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
    // ----- File -----
    m_fileMenu = menuBar->addMenu(tr("&File"));
    auto *fileMenu = m_fileMenu;
    m_actOpenDir = new QAction(tr("&Open Folder..."), this);
    m_actOpenDir->setShortcut(QKeySequence::Open); // Ctrl+O
    m_actOpenFile = new QAction(tr("Open &File..."), this);
    m_actOpenFile->setShortcut(QKeySequence("Ctrl+Shift+O"));
    m_actSaveWorkspace = new QAction(tr("&Save Workspace"), this);
    m_actOpenWorkspace = new QAction(tr("Open &Workspace"), this);
    m_actSaveProject = new QAction(tr("Save &Project"), this);
    m_actOpenProject = new QAction(tr("Open Pro&ject"), this);
    m_actExit = new QAction(tr("E&xit"), this);
    m_actExit->setShortcut(QKeySequence::Quit); // Ctrl+Q
    fileMenu->addAction(m_actOpenDir);
    fileMenu->addAction(m_actOpenFile);
    fileMenu->addSeparator();

    // P0: Recent folders (from core::RecentFiles LRU) + Favorites (pinned).
    m_recentMenu = fileMenu->addMenu(tr("&Recent Folders"));
    m_recentFileMenu = fileMenu->addMenu(tr("Recent &Files"));
    m_favMenu = fileMenu->addMenu(tr("Fa&vorite Folders"));
    m_actAddFavorite = new QAction(tr("Add Current Folder to Favorites(&D)"), this);
    m_actAddFavorite->setObjectName("addFavoriteAction");
    m_actAddFavorite->setShortcut(QKeySequence("Ctrl+D")); // Ctrl+D
    m_actRemoveFavorite = new QAction(tr("Remove Current Folder from Favorites"), this);
    m_actRemoveFavorite->setObjectName("removeFavoriteAction");
    fileMenu->addAction(m_actAddFavorite);
    fileMenu->addAction(m_actRemoveFavorite);

    fileMenu->addSeparator();
    fileMenu->addAction(m_actSaveWorkspace);
    fileMenu->addAction(m_actOpenWorkspace);
    fileMenu->addAction(m_actSaveProject);
    fileMenu->addAction(m_actOpenProject);
    m_actExportReport = new QAction(tr("Export &Report..."), this);
    m_actExportImages = new QAction(tr("&Export Images..."), this);
    fileMenu->addAction(m_actExportReport);
    fileMenu->addAction(m_actExportImages);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actExit);
}

void MainWindow::buildEditMenu(QMenuBar *menuBar)
{
    // ----- Edit — A-10: Undo/Redo -----
    m_editMenu = menuBar->addMenu(tr("&Edit"));
    auto *editMenu = m_editMenu;
    m_actUndo = new QAction("Undo(&U)", this);
    m_actUndo->setShortcut(QKeySequence::Undo); // Ctrl+Z
    m_actUndo->setEnabled(false);
    m_actRedo = new QAction("Redo(&R)", this);
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
                        QMessageBox::warning(this, tr("Undo Failed"),
                                             mviewer::ui::fileOpErrorText(err));
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
                        QMessageBox::warning(this, tr("Redo Failed"),
                                             mviewer::ui::fileOpErrorText(err));
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
    m_actSelectAll = new QAction(tr("Select All(&A)"), this);
    m_actSelectAll->setObjectName("selectAllAction");
    m_actSelectAll->setShortcut(QKeySequence::SelectAll);
    m_actDeselectAll = new QAction(tr("Deselect (&D)"), this);
    m_actDeselectAll->setObjectName("deselectAllAction");
    m_actDeselectAll->setShortcut(QKeySequence("Ctrl+Shift+A"));
    m_actInvertSelection = new QAction(tr("Invert Selection(&I)"), this);
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
    addOpenTrashEditAction(editMenu);
}

void MainWindow::addOpenTrashEditAction(QMenu *editMenu)
{
    editMenu->addSeparator();
    auto *openTrash = new QAction(tr("Open Recycle Bin"), this);
    openTrash->setObjectName(QStringLiteral("openTrashAction"));
    editMenu->addAction(openTrash);
    connect(openTrash, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->openTrashFolder();
            });
}

namespace
{
QString actTip(const QString &label, const QAction *action)
{
    return mviewer::ui::appendShortcutHint(label, mviewer::ui::actionShortcutHint(action));
}
} // namespace

void MainWindow::buildEditTransformActions(QMenu *editMenu)
{
    editMenu->addSeparator();
    m_actRotateCW = new QAction(tr("Rotate 90° Clockwise (&R)"), this);
    m_actRotateCW->setObjectName("rotateCWAction");
    m_actRotateCW->setShortcut(QKeySequence("Ctrl+R"));
    m_actRotateCW->setToolTip(
        actTip(tr("Rotate 90° clockwise and overwrite the original file"), m_actRotateCW));
    m_actRotateCW->setEnabled(false);
    m_actRotateCCW = new QAction(tr("Rotate 90° Counterclockwise(&L)"), this);
    m_actRotateCCW->setObjectName("rotateCCWAction");
    m_actRotateCCW->setShortcut(QKeySequence("Ctrl+Shift+R"));
    m_actRotateCCW->setToolTip(
        actTip(tr("Rotate 90° counterclockwise and overwrite original file"), m_actRotateCCW));
    m_actRotateCCW->setEnabled(false);
    editMenu->addAction(m_actRotateCW);
    editMenu->addAction(m_actRotateCCW);
    connect(m_actRotateCW, &QAction::triggered, this, [this]() { rotateCurrentImage(90); });
    connect(m_actRotateCCW, &QAction::triggered, this, [this]() { rotateCurrentImage(-90); });
    m_actFlipH = new QAction(tr("Flip Horizontal(&H)"), this);
    m_actFlipH->setObjectName("flipHAction");
    m_actFlipH->setShortcut(QKeySequence("Ctrl+Shift+H"));
    m_actFlipH->setToolTip(actTip(tr("Flip Horizontal and Overwrite Original File"), m_actFlipH));
    m_actFlipH->setEnabled(false);
    m_actFlipV = new QAction(tr("Flip Vertical(&V)"), this);
    m_actFlipV->setObjectName("flipVAction");
    m_actFlipV->setShortcut(QKeySequence("Ctrl+Shift+V"));
    m_actFlipV->setToolTip(actTip(tr("Flip Vertically and Overwrite Original File"), m_actFlipV));
    m_actFlipV->setEnabled(false);
    editMenu->addAction(m_actFlipH);
    editMenu->addAction(m_actFlipV);
    connect(m_actFlipH, &QAction::triggered, this, [this]() { flipCurrentImage(true); });
    connect(m_actFlipV, &QAction::triggered, this, [this]() { flipCurrentImage(false); });
}

void MainWindow::buildViewMenu(QMenuBar *menuBar)
{
    // ----- View -----
    m_viewMenu = menuBar->addMenu(tr("&View"));
    auto *viewMenu = m_viewMenu;
    m_actCompare = new QAction(tr("Compare Mode (&C)"), this);
    m_actCompare->setObjectName("compareAction");
    m_actCompare->setShortcut(QKeySequence("P"));
    m_actCompare->setToolTip(tr("Select 2–8 images, then press P or C to open Compare"));
    m_actToggleAnalysis = new QAction(tr("Analysis Panel(&H)"), this);
    m_actToggleAnalysis->setObjectName("toggleAnalysisPanelAction");
    m_actToggleAnalysis->setCheckable(true);
    m_actToggleAnalysis->setChecked(false);
    m_actToggleAnalysis->setShortcut(QKeySequence(QStringLiteral("Alt+H")));
    // P0: in-session browse history (browser-style back/forward).
    m_actHistoryBack = new QAction(tr("&Back"), this);
    m_actHistoryBack->setObjectName("historyBackAction");
    m_actHistoryBack->setShortcut(QKeySequence::Back); // Alt+Left
    m_actHistoryForward = new QAction(tr("Forward(&N)"), this);
    m_actHistoryForward->setObjectName("historyForwardAction");
    m_actHistoryForward->setShortcut(QKeySequence::Forward); // Alt+Right
    // P0: Directory-level back/forward (independent of image history).
    m_actDirBack = new QAction("Previous directory", this);
    m_actDirBack->setObjectName("directoryBackAction");
    m_actDirBack->setShortcut(QKeySequence("Ctrl+Alt+Left"));
    m_actDirForward = new QAction("Next directory", this);
    m_actDirForward->setObjectName("directoryForwardAction");
    m_actDirForward->setShortcut(QKeySequence("Ctrl+Alt+Right"));
    viewMenu->addAction(m_actHistoryBack);
    viewMenu->addAction(m_actHistoryForward);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actDirBack);
    viewMenu->addAction(m_actDirForward);
    m_actSplitBrowse = new QAction(tr("Split Browse View"), this);
    m_actSplitBrowse->setObjectName("splitBrowseAction");
    m_actSplitBrowse->setCheckable(true);
    m_actSplitBrowse->setChecked(false);
    m_actSplitBrowse->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(m_actSplitBrowse, &QAction::toggled, this, &MainWindow::toggleSplitBrowse);
    viewMenu->addAction(m_actSplitBrowse);
    viewMenu->addAction(m_actCompare);
    viewMenu->addAction(m_actToggleAnalysis);
    m_actToggleSearch = new QAction(tr("Global &Search"), this);
    m_actToggleSearch->setObjectName("toggleSearchPanelAction");
    m_actToggleSearch->setCheckable(true);
    m_actToggleSearch->setChecked(false);
    m_actToggleSearch->setShortcut(QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction(m_actToggleSearch);
    m_actFocusBrowse = new QAction(tr("Focus Browse Mode (Tab)"), this);
    m_actFocusBrowse->setObjectName("focusBrowseAction");
    m_actFocusBrowse->setCheckable(true);
    m_actFocusBrowse->setShortcut(QKeySequence(Qt::Key_Tab));
    m_actFocusBrowse->setShortcutContext(Qt::WindowShortcut);
    viewMenu->addAction(m_actFocusBrowse);
    addAction(m_actFocusBrowse);
    m_actBrowseWorkspace = new QAction(tr("Browse Layout"), this);
    m_actBrowseWorkspace->setObjectName("browseWorkspaceAction");
    m_actBrowseWorkspace->setCheckable(true);
    m_actBrowseWorkspace->setChecked(true);
    m_actBrowseWorkspace->setToolTip(
        tr("Hide analysis and search panels, keep folder and preview navigation"));
    viewMenu->addAction(m_actBrowseWorkspace);
    m_actToggleMetadata = new QAction(tr("Image &Info"), this);
    m_actToggleMetadata->setObjectName("toggleMetadataAction"); // stable test discovery
    m_actToggleMetadata->setCheckable(true);
    m_actToggleMetadata->setChecked(false);
    m_actToggleMetadata->setShortcut(QKeySequence("Ctrl+I"));
    viewMenu->addAction(m_actToggleMetadata);
    viewMenu->addSeparator();
    buildViewZoomActions(viewMenu);
    m_actSlideshow = new QAction(tr("&Slideshow (S)"), this);
    m_actSlideshow->setObjectName("slideshowAction");
    m_actSlideshow->setCheckable(true);
    viewMenu->addAction(m_actSlideshow);
    viewMenu->addSeparator();
    m_actColorManagement = new QAction(tr("Color Management (CMS)"), this);
    m_actColorManagement->setObjectName("colorManagementAction");
    m_actColorManagement->setCheckable(true);
    m_actColorManagement->setChecked(DisplayColorContextProvider::isColorManagementEnabled());
    m_actColorManagement->setToolTip(
        tr("When enabled, convert colors using the display ICC profile; when disabled, use native "
           "RGB/sRGB (matches FastStone)"));
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
    // ----- Tools -----
    m_toolsMenu = menuBar->addMenu(tr("&Tools"));
    auto *toolsMenu = m_toolsMenu;
    m_actBatch = new QAction(tr("&Batch Processing"), this);
    m_actBatch->setShortcut(QKeySequence("Ctrl+Shift+B"));
    toolsMenu->addAction(m_actBatch);
    // M17: batch analyzer export — same path as gallery context menu.
    auto *actBatchAnalyze = new QAction(tr("Batch Analysis Export(&A)..."), this);
    actBatchAnalyze->setShortcut(QKeySequence("Ctrl+Alt+A"));
    toolsMenu->addAction(actBatchAnalyze);
    connect(actBatchAnalyze, &QAction::triggered, this,
            [this]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->batchAnalyzeExport();
            });
    m_actPluginSettings = new QAction(tr("&Plugin Manager..."), this);
    toolsMenu->addAction(m_actPluginSettings);

    QAction *actPrefs = new QAction(tr("Preferences(&O)..."), this);
    connect(actPrefs, &QAction::triggered, this, &MainWindow::openPreferences);
    toolsMenu->addAction(actPrefs);

    QAction *actOverlay = new QAction(tr("Analysis Overlay / Scopes..."), this);
    connect(actOverlay, &QAction::triggered, this, &MainWindow::openAnalysisOverlay);
    toolsMenu->addAction(actOverlay);

    m_actUsmCompare = new QAction(tr("USM Sharpen Compare (&U)..."), this);
    m_actUsmCompare->setShortcut(QKeySequence("Ctrl+U"));
    connect(m_actUsmCompare, &QAction::triggered, this, [this]() { openUsmCompare(); });
    toolsMenu->addAction(m_actUsmCompare);

    toolsMenu->addSeparator();
    m_actExportSettings = new QAction(tr("&Export Settings..."), this);
    m_actImportSettings = new QAction(tr("&Import Settings..."), this);
    toolsMenu->addAction(m_actExportSettings);
    toolsMenu->addAction(m_actImportSettings);
    toolsMenu->addSeparator();
    // M21: Memory Timeline snapshot (status bar + optional CSV dump).
    auto *actMemTimeline = new QAction(tr("&Memory Timeline..."), this);
    actMemTimeline->setToolTip(
        tr("Sample the MemoryTracker timeline and show peak/recent samples"));
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
                QMessageBox::information(this, tr("Memory Timeline"), msg);
            });
    toolsMenu->addAction(actMemTimeline);

    // ----- Help -----
    m_helpMenu = menuBar->addMenu(tr("&Help"));
    auto *helpMenu = m_helpMenu;
    auto *actCheckUpdate = new QAction(tr("Check for &Updates..."), this);
    connect(actCheckUpdate, &QAction::triggered, this, [this]() { checkForUpdates(false); });
    helpMenu->addAction(actCheckUpdate);
    helpMenu->addSeparator();
    auto *actGuide = new QAction(tr("User &Guide"), this);
    connect(actGuide, &QAction::triggered, this, &MainWindow::showUserGuide);
    helpMenu->addAction(actGuide);
    auto *actShortcuts = new QAction(tr("&Keyboard Shortcuts"), this);
    actShortcuts->setShortcut(QKeySequence(Qt::Key_F1));
    connect(actShortcuts, &QAction::triggered, this, &MainWindow::showShortcutsHelp);
    helpMenu->addAction(actShortcuts);
    m_actAbout = new QAction(tr("&About"), this);
    helpMenu->addAction(m_actAbout);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    struct ActionEntry
    {
        QAction *act;
        const char *text;
    };
    const ActionEntry actions[] = {
        {m_actOpenDir, QT_TR_NOOP("&Open Folder...")},
        {m_actOpenFile, QT_TR_NOOP("Open &File...")},
        {m_actSaveWorkspace, QT_TR_NOOP("&Save Workspace")},
        {m_actOpenWorkspace, QT_TR_NOOP("Open &Workspace")},
        {m_actSaveProject, QT_TR_NOOP("Save &Project")},
        {m_actOpenProject, QT_TR_NOOP("Open Pro&ject")},
        {m_actExportReport, QT_TR_NOOP("Export &Report...")},
        {m_actExportImages, QT_TR_NOOP("&Export Images...")},
        {m_actExit, QT_TR_NOOP("E&xit")},
        {m_actAddFavorite, QT_TR_NOOP("Add Current Folder to Favorites(&D)")},
        {m_actRemoveFavorite, QT_TR_NOOP("Remove Current Folder from Favorites")},
        {m_actUndo, QT_TR_NOOP("Undo(&U)")},
        {m_actRedo, QT_TR_NOOP("Redo(&R)")},
        {m_actRotateCW, QT_TR_NOOP("Rotate 90° Clockwise (&R)")},
        {m_actRotateCCW, QT_TR_NOOP("Rotate 90° Counterclockwise(&L)")},
        {m_actFlipH, QT_TR_NOOP("Flip Horizontal(&H)")},
        {m_actFlipV, QT_TR_NOOP("Flip Vertical(&V)")},
        {m_actSelectAll, QT_TR_NOOP("Select All(&A)")},
        {m_actDeselectAll, QT_TR_NOOP("Deselect (&D)")},
        {m_actInvertSelection, QT_TR_NOOP("Invert Selection(&I)")},
        {m_actSplitBrowse, QT_TR_NOOP("Split Browse View")},
        {m_actCompare, QT_TR_NOOP("Compare Mode (&C)")},
        {m_actToggleAnalysis, QT_TR_NOOP("Analysis Panel(&H)")},
        {m_actHistoryBack, QT_TR_NOOP("&Back")},
        {m_actHistoryForward, QT_TR_NOOP("Forward(&N)")},
        {m_actDirBack, QT_TR_NOOP("Previous directory")},
        {m_actDirForward, QT_TR_NOOP("Next directory")},
        {m_actToggleSearch, QT_TR_NOOP("Global &Search")},
        {m_actFocusBrowse, QT_TR_NOOP("Focus Browse Mode (Tab)")},
        {m_actBrowseWorkspace, QT_TR_NOOP("Browse Workspace")},
        {m_actDirUp, QT_TR_NOOP("Parent Directory(&U)")},
        {m_actRefresh, QT_TR_NOOP("Refresh(&R)")},
        {m_actOpenCurrentFolder, QT_TR_NOOP("Open Current Folder In File Manager")},
        {m_actBatch, QT_TR_NOOP("&Batch Processing...")},
        {m_actUsmCompare, QT_TR_NOOP("USM Sharpen Compare (&U)...")},
        {m_actPluginSettings, QT_TR_NOOP("&Plugin Settings...")},
        {m_actAbout, QT_TR_NOOP("&About")},
        {m_actZoomIn, QT_TR_NOOP("Zoom &In")},
        {m_actZoomOut, QT_TR_NOOP("Zoom &Out")},
        {m_actZoomFit, QT_TR_NOOP("&Fit to Window")},
        {m_actZoomActual, QT_TR_NOOP("&Actual Size")},
        {m_actFullscreen, QT_TR_NOOP("&Fullscreen")},
        {m_actSlideshow, QT_TR_NOOP("&Slideshow")},
    };
    for (const auto &entry : actions)
    {
        if (entry.act)
            entry.act->setText(tr(entry.text));
    }

    struct MenuEntry
    {
        QMenu *menu;
        const char *text;
    };
    const MenuEntry menus[] = {
        {m_fileMenu, QT_TR_NOOP("&File")},
        {m_editMenu, QT_TR_NOOP("&Edit")},
        {m_viewMenu, QT_TR_NOOP("&View")},
        {m_toolsMenu, QT_TR_NOOP("&Tools")},
        {m_helpMenu, QT_TR_NOOP("&Help")},
        {m_recentMenu, QT_TR_NOOP("&Recent Folders")},
        {m_recentFileMenu, QT_TR_NOOP("Recent &Files")},
        {m_favMenu, QT_TR_NOOP("Fa&vorite Folders")},
    };
    for (const auto &entry : menus)
    {
        if (entry.menu)
            entry.menu->setTitle(tr(entry.text));
    }
}

void MainWindow::buildViewZoomActions(QMenu *viewMenu)
{
    m_actZoomIn = new QAction(tr("&Zoom In"), this);
    m_actZoomIn->setObjectName("zoomInAction");
    m_actZoomIn->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    m_actZoomOut = new QAction(tr("Zoom &Out"), this);
    m_actZoomOut->setObjectName("zoomOutAction");
    m_actZoomOut->setShortcut(QKeySequence("Ctrl+-"));
    m_actZoomFit = new QAction(tr("&Fit to Window"), this);
    m_actZoomFit->setObjectName("zoomFitAction");
    m_actZoomFit->setShortcut(QKeySequence("Ctrl+0"));
    m_actZoomActual = new QAction(tr("&Actual Size"), this);
    m_actZoomActual->setObjectName("zoomActualAction");
    m_actFullscreen = new QAction("Full Screen(&U)", this);
    m_actFullscreen->setShortcut(QKeySequence("F11"));
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addAction(m_actZoomActual);
    m_zoomPresetsMenu = viewMenu->addMenu(tr("Zoom Presets"));
    m_zoomPresetsMenu->setObjectName("zoomPresetsMenu");
    m_zoomPresetsMenu->addAction("50%", this, [this]() { zoomViewer(4); });
    m_zoomPresetsMenu->addAction(tr("100% (Actual Size)"), this, [this]() { zoomViewer(3); });
    m_zoomPresetsMenu->addAction("200%", this, [this]() { zoomViewer(5); });
    m_zoomPresetsMenu->addAction("400%", this, [this]() { zoomViewer(6); });
    m_zoomPresetsMenu->addAction(tr("800% (Pixel Grid)"), this, [this]() { zoomViewer(7); });
    viewMenu->addSeparator();
    viewMenu->addAction(m_actFullscreen);
}
