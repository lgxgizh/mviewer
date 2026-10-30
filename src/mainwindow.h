#pragma once

#include "appstate.h"
#include "core/analysis/ReportHtml.h"
#include "core/command/CommandRegistry.h"
#include "core/command/CommandStack.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/update/UpdateChecker.h"
#include "core/update/UpdatePrompt.h"
#include "core/workspace/WorkspaceSerializer.h"

#include "secondarybrowsehistory.h"
#include "selectionmodel.h"
#include "splitbrowse_selection.h"
#include <QKeyEvent>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <optional>

class ImageViewer;
class ImageFrame;
class DirectoryTree;
class DirectoryMonitor;
class BreadcrumbBar;
class MetadataOverlay;
class ThumbnailPanel;
class PreviewPanel;
class AnalysisPanel;
class MetadataPanel;
class CompareWorkspace;
class SearchPanel;
class DirectoryModel;
class ImageListModel;
class WorkspaceModel;
class AnalyzerModel;
class BatchDialog;
class PluginSettings;
class PreferencesDialog;
class AnalysisOverlayDialog;
class FormatFilterCombo;
class QAction;
class QMenu;
class QMenuBar;
class QLineEdit;
class QCheckBox;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QTimer;
class QComboBox;
class QPushButton;
class QSplitter;
class QSlider;
class QToolBar;
class QProgressDialog;
class QSettings;
class QImage;

namespace mviewer::core
{
class Histogram;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Public so a headless screenshot harness (M11.3 release artifact) can build
    // the real UI and render it to a pixmap without a visible window.
    void setupUi();
    void onImageOpen(const QString &path);
    void setOpenOnLaunch(const QString &path);
    void setOpenOnLaunch(const QStringList &paths);
    // Shared command-line, shell-association, and drag/drop dispatch. Invalid
    // or mixed targets are rejected as one transaction and never partially
    // open a different target.
    void openExternalTargets(const QStringList &paths);

    // M47: dialog-free workspace/project restore. The file is read and
    // deserialized on a background worker; the UI applies the parsed document
    // atomically and only for the latest request (a newer open supersedes an
    // in-flight one; a failed open never touches the live session). Public so
    // tests, drop targets, and CLI callers can restore without a file dialog.
    void openWorkspaceFile(const QString &filePath);
    void openProjectFile(const QString &filePath);

    // Split Browse View (RFC ADR-001/005/014/018)
    bool isSplitBrowseEnabled() const
    {
        return m_splitBrowseEnabled;
    }
    void toggleSplitBrowse(bool enabled);
    void openRightDirectory(const QString &dir);
    // Browse file type filter (browseformats.h group ids), persisted and
    // applied to both panes; both type combos stay in sync.
    void setBrowseHiddenFormatGroups(const QStringList &groups);
    QStringList browseHiddenFormatGroups() const;
    // Route a pane's "Show hidden files" hint click to the shared type filter.
    void wireTypeHiddenHint(ThumbnailPanel *panel);
    void swapBrowsePanes();
    void cloneLeftToRightPane();
    void setActiveBrowsePane(SelectionModel::PaneSlot pane);
    SelectionModel::PaneSlot activeBrowsePane() const
    {
        return m_activeBrowsePane;
    }
    ThumbnailPanel *activeThumbnailPanel() const;
    ThumbnailPanel *secondaryThumbnailPanel() const
    {
        return m_secondaryThumbnailPanel;
    }
    void updateSplitBrowseCompareButton();
    QStringList sortPathsForCompare(const QStringList &paths) const;
    QWidget *primaryBrowsePaneWidget() const
    {
        return m_primaryBrowsePane;
    }
    QWidget *secondaryBrowsePaneWidget() const
    {
        return m_secondaryBrowsePane;
    }
    SelectionModel *selectionModel() const
    {
        return m_selection;
    }
    QAction *compareAction() const
    {
        return m_actCompare;
    }

    // The shortcut cheat-sheet HTML (single source of truth for the F1 help;
    // public so tests can verify it stays in sync with registered commands).
    static QString shortcutsHelpHtml();
    static QString userGuideHtml();

  protected:
    void changeEvent(QEvent *event) override;
    void retranslateUi();
    void closeEvent(QCloseEvent *event) override;
    // M15: drag & drop support
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    // A-5: keep floating MetadataPanel docked to the right edge on move/resize.
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // P0-3: intercept image-viewer mouse events for metadata overlay triggers.
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool filterKeyPress(QObject *watched, QKeyEvent *ke);
    bool routeViewerFileOpKeys(QObject *watched, QKeyEvent *ke, Qt::KeyboardModifiers mods);

  private:
    void buildMenus();
    void buildFileMenu(QMenuBar *menuBar);
    void buildEditMenu(QMenuBar *menuBar);
    void addOpenTrashEditAction(QMenu *editMenu);
    void buildEditTransformActions(QMenu *editMenu);
    void buildViewMenu(QMenuBar *menuBar);
    void buildViewZoomActions(QMenu *viewMenu);
    void buildToolsHelpMenus(QMenuBar *menuBar);
    void buildBrowserShell();
    void buildUnifiedToolbar();
    QWidget *makeBrowserLocation(QWidget *parent);
    void addTrailingBrowserActions(QToolBar *bar);
    void addComparePill(QWidget *cluster);
    void attachUnifiedToolbarControls();
    void refreshBrowserLocation(const QString &path);
    void syncViewModeSegmentToolTips();
    void installNavigationCollapse();
    void polishGalleryToolbar(QWidget *sortBar);
    void placeGalleryCompareButton(QWidget *sortBar);
    void dockCompareButton(ThumbnailPanel *panel, QWidget *host, QHBoxLayout *row, QWidget *after);
    QWidget *buildNavigationPanel();
    QWidget *buildGalleryPanel();
    QWidget *buildSecondaryBrowsePane();
    QWidget *buildSecondaryHeaderBar(QWidget *pane);
    void wireSecondaryPanelSignals();
    void wireSecondaryBrowseHostSignals();
    QHBoxLayout *buildSecondarySortControls(QWidget *headerBar, QVBoxLayout *headerLayout);
    void applyGallerySplitterSizes();
    void updateActivePaneIndicator();
    void updateSwapButtonEnabled();
    void syncOtherPaneDirectories();
    void updateOtherPaneTransferActions();
    void copyActiveSelectionToOtherPane();
    void moveActiveSelectionToOtherPane();
    void onFilesTransferred(const QString &destinationDirectory, const QStringList &createdPaths);
    void navigateSecondaryBack();
    void navigateSecondaryForward();
    void navigateSecondaryUp();
    void updateSecondaryNavButtons();
    // Split browse with the right pane active: the toolbar's back / forward /
    // up (and their shortcuts) act on the right pane's history.
    bool secondaryNavActive() const;
    bool routeSecondaryBrowseKey(QObject *watched, QEvent *event);
    bool routeDirectoryHistoryMouse(QObject *watched, QEvent *event);
    bool secondaryBrowseOwns(QObject *watched) const;
    void enableSplitBrowseUi();
    void mirrorPrimaryIntoSecondary();
    void disableSplitBrowseUi();
    void applySecondarySortFromState();
    void persistSecondaryBrowseState();
    void restoreSecondaryBrowsePosition();
    void applyPendingSecondaryBrowseRestore(const QString &directory, const QStringList &paths);
    void handleSecondaryDroppedPaths(const QStringList &paths);
    void onGalleryPathsRemoved(ThumbnailPanel *source, const QStringList &deleted);
    void onGalleryPathsRenamed(const QStringList &oldPaths, const QStringList &newPaths);
    void onGalleryPathsModified(const QStringList &paths);
    void dropRemovedPathsFromPanel(ThumbnailPanel *panel, const QStringList &deleted);
    void refreshVisibleBrowsePanes();
    void applyActivePaneStatusBar();
    void onSecondaryStatsChanged(int total, qint64 totalBytes, int selected, qint64 selectedBytes);
    void onSecondarySequenceChanged(const QString &directory, const QStringList &paths);
    void onBrowseFocusChanged(QWidget *old, QWidget *now);
    ThumbnailPanel *thumbnailPanelForPath(const QString &path,
                                          ThumbnailPanel *prefer = nullptr) const;
    void onImageOpenFromPanel(const QString &path, ThumbnailPanel *source);
    QWidget *buildSortBar(QWidget *parent);
    void buildPrimarySortControls(QWidget *sortBar, QHBoxLayout *sortLayout);
    void buildAdvancedFilterControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                                     QWidget *advancedFilterPanel, QHBoxLayout *advancedLayout);
    void buildSearchControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                             QWidget *advancedFilterPanel, QHBoxLayout *advancedLayout);
    void applySortComboItems(QComboBox *combo);
    void applyViewModeComboItems(QComboBox *combo);
    void applyRatingFilterItems(QComboBox *combo);
    void applyFlagFilterItems(QComboBox *combo);
    QString sortDirectionToolTip(bool detailed) const;
    void retranslateBrowseBar();
    void retranslateSplitHeader();
    void buildAnalysisAndSearchPanels();
    void buildCentralContainer(QWidget *leftWidget, QWidget *rightWidget);
    void buildMetadataPanelUi();
    void buildImageViewerUi();
    void connectUiSignals();
    void connectNavigationSignals();
    void connectGallerySignals();
    void connectSelectionSignals();
    void connectViewerSignals();
    void wireHostedFileTransforms();
    void connectRawSourceUi();
    void connectFilterSignals();
    void connectMenuSignals();
    void connectWorkspaceSignals();
    void connectPanelSignals();
    void connectSettingsSignals();
    void buildStatusBarUi();

    void setupCommands();
    // Ctrl+F / Alt+D. Activates this window so the address bar becomes
    // QApplication::focusWidget() even when the viewer window has the key.
    void focusAddressBar();
    bool forwardGlobalShortcut(QObject *watched, QKeyEvent *ke, Qt::KeyboardModifiers mods);
    bool handleWindowKey(QKeyEvent *event);
    bool handleMetadataKey(QKeyEvent *event);
    bool handleViewModeKey(QKeyEvent *event);
    bool handleClipboardKey(QKeyEvent *event);
    void handleCopyKey(bool copyPath);
    bool browseOwnsCopyKey() const;
    bool copyBrowseFilesToClipboard();
    bool isThumbnailPanelPasteTarget() const;
    void startClipboardPaste(const QImage &image);
    void cancelClipboardPaste();
    void cleanupClipboardPasteTemps();
    bool handleViewerKey(QKeyEvent *event);
    void openCompare(const QStringList &images = {}, const QString &sessionJson = {});
    void showCompareDialog(const QStringList &images, const QString &sessionJson);
    void navigate(int delta);
    // P1-8: Home/End/PageUp/PageDown navigation within the current folder.
    void navigatePage(int key);
    void onBreadcrumbPath(const QString &path);
    // P0-2: central handler that keeps every view synced with the current image.
    void onCurrentImageChanged(const QString &path);
    // A gallery thumbnail landed after the selection moved to `path`: offer it
    // to the preview panel and (when on screen) the viewer as provisional.
    void onPanelThumbnailReady(ThumbnailPanel *panel, const QString &path);
    // Source display size / file size known for `path` without file I/O: the
    // gallery entry, else the size the thumbnail provider learned.
    static QSize knownSourceSizeFor(const ThumbnailPanel *panel, const QString &path,
                                    qint64 *fileSize);
    // P1-8: keyboard-shortcut cheat-sheet dialog (F1 / Help menu).
    void showShortcutsHelp();
    void showUserGuide();
    void openDirectory(const QString &dir);
    // Unified directory-change handler: updates directory tree (which triggers
    // the directoryChanged signal chain), thumbnail panel, breadcrumb, recent
    // folders, status bar, etc. Used by the menu "Open Directory" action, the
    // path input bar, and breadcrumb navigation.
    void changeDirectory(const QString &dir);
    void scheduleSidecarImport(const QString &dir);
    void scheduleSidecarImportPaths(const QStringList &imagePaths);
    // Shows/hides the gallery empty-state hint (no directory open yet).
    void updateEmptyState();
    void buildEmptyStateUi();
    void layoutEmptyState();
    // Shows/hides the gallery empty-folder hint (directory open, no images).
    void updateEmptyFolderState();
    // Toggles fullscreen on the image viewer when it is visible, else on the
    // main window itself. Shared by the F command, F11 and the View menu.
    void toggleFullscreen();
    // Forwards a zoom command to the viewer when it is on screen.
    void zoomViewer(int op); // 0=in, 1=out, 2=fit, 3=actual, 4=50%, 5=200%, 6=400%, 7=800%
    void showZoomPresetMenu(const QPoint &globalPos);

    // P0: product browse state — recent folders, favorites, in-session history,
    // and cross-session restore.
    void pushHistory(const QString &path);
    void navigateHistory(int delta);
    void rebuildRecentMenu();
    void rebuildRecentFilesMenu();
    void rebuildFavoritesMenu();
    void rebuildFavoritesBar();
    void addFavoriteCurrent();
    void removeFavorite(const QString &dir = {});
    void restoreWindowSettings();
    void restoreNavigationSettings(const QSettings &settings);
    void restoreLastSession();
    void restoreSessionPostScroll();
    // P0: Directory-level back/forward history (independent of image history).
    void pushDirHistory(const QString &dir);
    void goDirBack();
    void goDirForward();

    // P0-3: metadata overlay — position and show the floating metadata panel.
    void showMetadataOverlay();
    void toggleMetadataOverlay();
    // P0-3: async metadata-overlay histogram. The full-image histogram is
    // computed ONLY on the Analysis pool (never the UI thread), latest-wins and
    // cancellable. Every show path funnels through scheduleMetadataHistogram();
    // navigation/hide/destruction cancel the in-flight task and invalidate the
    // generation so a late delivery can never touch a stale or freed overlay.
    void cancelMetadataHistogram();
    void scheduleMetadataHistogram();
    void applyMetadataHistogram(uint64_t gen, const QString &path,
                                const mviewer::core::Histogram &hist);
    TaskScheduler::TaskHandle m_metadataHistTask;  // newest owned Analysis task
    uint64_t m_metadataHistGen = 0;                // generation guard (latest-wins)
    QString m_metadataHistPath;                    // image the newest task targets
    std::weak_ptr<ImageFrame> m_metadataHistFrame; // frame identity token
    // Post-delivery dedup: once a histogram lands, the completed task handle is
    // released and this flag records that the CURRENT path+frame is already
    // delivered. Repeated show notifications (hover, action, imageReady) for the
    // SAME already-delivered frame then short-circuit without resubmitting; a new
    // path/frame, hide, navigation, or cancellation clears it so a genuinely new
    // request still runs.
    bool m_metadataHistDelivered = false;
    uint64_t m_statusMetadataGeneration = 0;
    std::string m_statusMetadataConsumer;
    void copyCurrentImageToClipboard();
    void openQuickCompare();
    void openPreferences(); // F1 (M22): centralized Preferences dialog
    // Re-apply the preferences that changed relative to `before` (a snapshot
    // taken when the dialog opened).
    void applyPreferences(const QVariantMap &before);
    void applyColorManagementPreference();
    void openAnalysisOverlay(); // F4 (M22): zebra / false-color / scopes dialog
    void openUsmCompare(const QString &path = {});
    void toggleFocusBrowse();

    void keyPressEvent(QKeyEvent *event) override;

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    DirectoryMonitor *m_directoryMonitor = nullptr;
    BreadcrumbBar *m_breadcrumb = nullptr;
    QLineEdit *m_pathEdit = nullptr;              // Address bar above sort/filter on gallery strip
    QSplitter *m_mainSplitter = nullptr;          // P1-3: central layout splitter
    QSplitter *m_leftSplitter = nullptr;          // A-6.4: nav | tree | preview heights
    QWidget *m_navigationWidget = nullptr;        // whole browse navigation column
    MetadataOverlay *m_metadataOverlay = nullptr; // M15: semi-transparent info overlay
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    bool m_splitBrowseEnabled = false;
    SelectionModel::PaneSlot m_activeBrowsePane = SelectionModel::PaneSlot::Primary;
    QSplitter *m_gallerySplitter = nullptr;
    QWidget *m_primaryBrowsePane = nullptr;
    QWidget *m_secondaryBrowsePane = nullptr;
    QLineEdit *m_secondaryPathEdit = nullptr;
    FormatFilterCombo *m_typeFilterCombo = nullptr;
    FormatFilterCombo *m_secondaryTypeFilterCombo = nullptr;
    QStringList m_hiddenFormatGroups;
    QPushButton *m_secondaryBrowseBtn = nullptr;
    QPushButton *m_secondaryCloneBtn = nullptr;
    QPushButton *m_secondarySwapBtn = nullptr;
    QPushButton *m_secondaryCloseBtn = nullptr;
    QComboBox *m_secondarySortCombo = nullptr;
    QPushButton *m_secondarySortDirBtn = nullptr;
    ThumbnailPanel *m_secondaryThumbnailPanel = nullptr;
    DirectoryMonitor *m_secondaryDirectoryMonitor = nullptr;
    bool m_pendingSecondaryScrollRestore = false;
    // Split entry source: session restore keeps the saved right pane; an
    // explicit right-pane target skips mirroring the left folder.
    bool m_splitRestoringSession = false;
    bool m_splitOpeningTarget = false;
    QAction *m_actSplitBrowse = nullptr;
    QLabel *m_emptyState = nullptr; // gallery empty-state hint (objectName: emptyStateLabel)
    QLabel *m_emptyFolderLabel =
        nullptr; // gallery empty-folder hint (objectName: emptyFolderLabel)
    QTimer *m_emptyFolderTimer = nullptr;    // defers the hint past the pre-scan zero
    QPushButton *m_emptyStateOpen = nullptr; // empty-state "open folder" (emptyStateOpenButton)
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    MetadataPanel *m_metadataPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;
    BatchDialog *m_batchDialog = nullptr;
    PluginSettings *m_pluginSettings = nullptr;
    CompareWorkspace *m_compareView = nullptr;
    QPointer<QWidget> m_compareHost; // host is a QDialog; typed as QWidget for safe destroy

    QAction *m_actOpenDir = nullptr;
    QAction *m_actOpenFile = nullptr;
    QAction *m_actSaveWorkspace = nullptr;
    QAction *m_actOpenWorkspace = nullptr;
    QAction *m_actSaveProject = nullptr;
    QAction *m_actOpenProject = nullptr;
    QAction *m_actExportReport = nullptr;
    QAction *m_actExportImages = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actCompare = nullptr;
    QAction *m_actToggleAnalysis = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actAddFavorite = nullptr;
    QAction *m_actRemoveFavorite = nullptr;
    QAction *m_actHistoryBack = nullptr;
    QAction *m_actHistoryForward = nullptr;
    QAction *m_actDirBack = nullptr;
    QAction *m_actDirForward = nullptr;
    QAction *m_actToggleSearch = nullptr;
    QAction *m_actFocusBrowse = nullptr;
    QAction *m_actBrowseWorkspace = nullptr;
    QAction *m_actDirUp = nullptr;
    QAction *m_actRefresh = nullptr;
    QAction *m_actOpenCurrentFolder = nullptr;
    QAction *m_actToggleNavigation = nullptr;
    QAction *m_actBatch = nullptr;
    QAction *m_actUsmCompare = nullptr;
    QAction *m_actDecodeFullRaw = nullptr;
    QAction *m_actPluginSettings = nullptr;
    QAction *m_actToggleMetadata = nullptr;
    QAction *m_actExportSettings = nullptr;
    QAction *m_actImportSettings = nullptr;
    // A-10: Undo/Redo via CommandStack.
    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    QAction *m_actRotateCW = nullptr;
    QAction *m_actRotateCCW = nullptr;
    QAction *m_actFlipH = nullptr;
    QAction *m_actFlipV = nullptr;
    QAction *m_actSelectAll = nullptr;
    QAction *m_actDeselectAll = nullptr;
    QAction *m_actInvertSelection = nullptr;
    QAction *m_actCopyOtherPane = nullptr;
    QAction *m_actMoveOtherPane = nullptr;
    QAction *m_actColorManagement = nullptr;
    CommandStack m_cmdStack;
    void updateUndoRedoActions();

    // Clipboard paste encoding runs off the UI thread. The generation/alive
    // pair prevents a late worker completion from reopening a destroyed or
    // superseded viewer request.
    TaskScheduler::TaskHandle m_clipboardPasteTask;
    std::shared_ptr<std::atomic<bool>> m_clipboardPasteAlive;
    uint64_t m_clipboardPasteGeneration = 0;
    QString m_clipboardPastePath;
    void positionMetadataPanel();
    // Zoom / fullscreen view commands (forwarded to the image viewer).
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actZoomFit = nullptr;
    QAction *m_actZoomActual = nullptr;
    QAction *m_actPixelGrid = nullptr;
    QAction *m_actFullscreen = nullptr;
    QMenu *m_zoomPresetsMenu = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_recentFileMenu = nullptr; // recent-files menu (opened images)
    QMenu *m_favMenu = nullptr;

    // M15: crash recovery
    QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveLoaded = false;
    // Set for the rest of close/teardown so a queued autosave cannot rewrite
    // the recovery file after a normal exit deleted it.
    bool m_sessionClosing = false;

    // P0-3: hover-activated metadata overlay.
    QTimer *m_metadataHoverTimer = nullptr;

    // M15 Sprint 2-1: global search index rebuild on directory change.
    void reindexSearch();
    // P0-1 (perf): building the search index reads full metadata for every
    // image, which used to run synchronously on the folder-switch path and froze
    // the UI. Debounce it so switching folders stays responsive; the index is
    // (re)built shortly after the user settles on a directory.
    QTimer *m_reindexTimer = nullptr;
    void scheduleReindex();
    // M25: generation guard for the async search re-index (a late completion
    // from a previous directory is dropped).
    uint64_t m_reindexGen = 0;
    // M26: the MetadataIndexer request owned by the search re-index — a newer
    // reindex supersedes ONLY this request, never the gallery's index.
    uint64_t m_reindexRequestId = 0;
    // M27: alive token for the in-flight re-index request; the destructor flips
    // it (and cancels the request) so late completions can never touch freed
    // MainWindow state.
    std::shared_ptr<std::atomic<bool>> m_reindexAlive;

    // M18: gallery search bar.
    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_searchRecursive = nullptr;
    // P1: metadata-aware search + star-rating filter.
    QCheckBox *m_searchMeta = nullptr;
    QComboBox *m_ratingFilter = nullptr;
    QComboBox *m_sortCombo = nullptr; // persisted across sessions via QSettings
    QPushButton *m_sortDirBtn = nullptr;
    QComboBox *m_viewModeCombo = nullptr;
    QComboBox *m_flagFilter = nullptr; // P3 tail: color label / reject / pick / recents
    QWidget *m_advancedFilterPanel = nullptr;
    bool m_focusBrowse = false;
    bool m_focusNavigationVisible = true;
    bool m_focusAnalysisVisible = false;
    bool m_focusSearchVisible = false;
    // Browse workspace is independent from Tab focus mode. Mixing these
    // snapshots makes one mode restore the other mode's panel state.
    bool m_browseAnalysisVisible = false;
    bool m_browseSearchVisible = false;
    // A directory switch clears the old selection immediately, then selects
    // exactly one first item when the new asynchronous gallery scan arrives.
    bool m_autoSelectFirstPending = false;

    // P0 #①: real-time status bar (image count / size / zoom / cache hit-rate).
    QLabel *m_lblImage = nullptr; // current image dimensions + file size
    QLabel *m_lblCount = nullptr;
    QLabel *m_lblSize = nullptr;
    QLabel *m_lblZoom = nullptr;
    QLabel *m_lblCache = nullptr;
    QLabel *m_lblRawSource = nullptr;
    QTimer *m_statTimer = nullptr;
    void updateCacheStat();
    static QString formatBytes(qint64 bytes);

    // P1: gallery search/filter controls.
    void onSearchMetaToggled(bool on);
    void onRatingFilterChanged(int index);
    void onFlagFilterChanged(int index);
    void onFlagsEdited(const QString &path, int label, bool rejected, bool picked);
    void rateCurrentImage(int stars);
    // P3 tail: shortcuts for color label / reject / pick on the current image.
    void setCurrentColorLabel(int label);
    void toggleCurrentPick();
    void toggleCurrentReject();
    void clearAllFilters();
    void resetFiltersOnFolderChange();

    // M19: UI models — single source of truth for Current / Selection /
    // Directory / ImageList / Workspace / Analyzer. Widgets listen; they do not
    // own a second copy of these states.
    SelectionModel *m_selection = nullptr;
    DirectoryModel *m_directory = nullptr;
    ImageListModel *m_imageList = nullptr;
    WorkspaceModel *m_workspace = nullptr;
    AnalyzerModel *m_analyzer = nullptr;
    // Guard against gallery ↔ SelectionModel selection feedback loops.
    bool m_syncingSelection = false;

    // In-session navigation history (like a browser back/forward).
    QStringList m_history;
    int m_historyIndex = -1;
    // P0: Directory-level back/forward history (separate from image history).
    QStringList m_dirHistory;
    int m_dirHistoryIndex = -1;
    SecondaryBrowseHistory m_secondaryBrowseHistory;
    // P0: Favorites bar widget in the left sidebar.
    QListWidget *m_favoritesBar = nullptr;
    // Persisted, cross-session app state (favorites + restore position).
    mviewer::core::RecentFiles m_recent;      // recent-folders LRU
    mviewer::core::RecentFiles m_recentFiles; // recent-files LRU (opened images)
    QStringList m_openOnLaunch;               // paths passed via command line
    bool m_openOnLaunchQueued = false;
    AppState m_appState;

    void saveWorkspace();
    void cancelBackgroundPersistence();
    void openWorkspace();
    void restoreWorkspaceState(const mviewer::domain::Workspace &workspace,
                               const QString &filePath);
    // M15 (Project): persist / restore the full evaluation environment as a
    // self-contained .mvproj file (datasets + compare session + analysis +
    // analyzer pipeline + export/review/benchmark config).
    void saveProject();
    void openProject();
    void exportReport();
    void startReportExport(mviewer::core::ReportContext context,
                           std::optional<mviewer::core::CompareReportInput> compareInput,
                           const QString &output, std::string suffix);
    void cancelReportExport();
    // P4: batch export pipeline entry point.
    void exportImages();
    // A-3: resolve paths for Export/Batch/Compare from SelectionModel first,
    // then fall back to gallery selection / visible set / full directory.
    QStringList resolveSelectedPaths(bool preferMulti = true) const;
    // A-3.4: enable/disable selection-dependent actions (Compare/Export/Batch).
    void updateSelectionActions();
    // Browse rotate/flip: SelectionModel::currentImage() is the only write
    // target (ADR-012). Compare still uses in-memory cell preview when up.
    void rotateCurrentImage(int degrees);
    void flipCurrentImage(bool horizontal);
    void runListedTransform(const QStringList &paths, bool rotate, int degrees, bool horizontal);
    void refreshTransformedPaths(const QStringList &paths);
    SplitSelectionRestorePlan captureTransformSelection() const;
    void applyTransformSelection(const SplitSelectionRestorePlan &plan);
    void deleteViewerImage();
    void renameViewerImage();
    void moveViewerImage();
    // Keep directory/image history and folder command boundaries in sync.
    void updateNavigationActions();
    // M19: ensure ImageListModel is populated for the current directory.
    void ensureImageList();
    // M19: apply SelectionModel multi-selection onto the gallery (one-way).
    void syncGalleryFromSelection();
    // Convenience accessors over the UI models (read-only mirrors for call sites).
    QString currentDir() const;
    QString currentImagePath() const;
    // M15: crash recovery
    void autosaveSession();
    void restoreSessionRecovery();
    void stopCloseTimers();
    void persistWindowLayoutOnClose(bool inBrowseWorkspace, QStringList &failures);
    void checkForUpdates(bool silent = false);
    void onUpdateChecked(const mviewer::core::UpdateInfo &info);
    void showUpdateDialog(const mviewer::core::UpdateInfo &info);
    void showUpdateNotice(const mviewer::core::UpdateInfo &info);
    bool updatePromptUiBusy() const;
    void maybeShowCrashReport();
    mviewer::core::UpdateCheckGate m_updateGate;
    mviewer::core::UpdateInfo m_pendingUpdate;
    bool m_hasPendingUpdate = false;
    QLabel *m_updateNotice = nullptr;

    // Shared drop handling for the main window and the thumbnail gallery.
    void handleDroppedPaths(const QStringList &paths);

    // M44: long-running persistence/report work is owned by the window and
    // invalidated before destruction.  The worker only receives value-owned
    // snapshots; UI delivery is generation guarded.
    TaskScheduler::TaskHandle m_persistenceTask;
    std::shared_ptr<std::atomic<bool>> m_persistenceCancel;
    uint64_t m_persistenceGeneration = 0;
    // M47: async workspace/project restore — latest-intent generation guard
    // (a newer open supersedes an in-flight one), cancelled on destruction.
    TaskScheduler::TaskHandle m_restoreTask;
    uint64_t m_restoreGeneration = 0;
    TaskScheduler::TaskHandle m_sidecarImportTask;
    std::shared_ptr<std::atomic<bool>> m_sidecarImportAlive;
    TaskScheduler::TaskHandle m_reportTask;
    QProgressDialog *m_reportProgress = nullptr;
    uint64_t m_reportGeneration = 0;

    // Slideshow: auto-advance through the current folder on a timer.
    void toggleSlideshow();
    void stopSlideshow();
    QTimer *m_slideshowTimer = nullptr;
    QAction *m_actSlideshow = nullptr;
    bool m_dragHighlight = false; // border highlight while a drag is hovering
};
