#pragma once

#include "appstate.h"
#include "core/command/CommandRegistry.h"
#include "core/command/CommandStack.h"
#include "core/update/UpdateChecker.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <QKeyEvent>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QStringList>

class ImageViewer;
class DirectoryTree;
class BreadcrumbBar;
class MetadataOverlay;
class ThumbnailPanel;
class PreviewPanel;
class AnalysisPanel;
class MetadataPanel;
class CompareWorkspace;
class SearchPanel;
class SelectionModel;
class DirectoryModel;
class ImageListModel;
class WorkspaceModel;
class AnalyzerModel;
class BatchDialog;
class PluginSettings;
class PreferencesDialog;
class AnalysisOverlayDialog;
class QAction;
class QMenu;
class QLineEdit;
class QCheckBox;
class QLabel;
class QTimer;
class QComboBox;
class QSplitter;
class QSlider;

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
    void setOpenOnLaunch(const QString &path)
    {
        m_openOnLaunch = path;
    }

  protected:
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

  private:
    void setupCommands();
    void openCompare(const QStringList &images = {}, const QString &sessionJson = {});
    void navigate(int delta);
    // P1-8: Home/End/PageUp/PageDown navigation within the current folder.
    void navigatePage(int key);
    void onBreadcrumbPath(const QString &path);
    // P0-2: central handler that keeps every view synced with the current image.
    void onCurrentImageChanged(const QString &path);
    // P1-8: keyboard-shortcut cheat-sheet dialog (F1 / Help menu).
    void showShortcutsHelp();
    void openDirectory(const QString &dir);
    // Unified directory-change handler: updates directory tree (which triggers
    // the directoryChanged signal chain), thumbnail panel, breadcrumb, recent
    // folders, status bar, etc. Used by the menu "Open Directory" action, the
    // path input bar, and breadcrumb navigation.
    void changeDirectory(const QString &dir);
    // Toggles fullscreen on the image viewer when it is visible, else on the
    // main window itself. Shared by the F command, F11 and the View menu.
    void toggleFullscreen();
    // Forwards a zoom command to the viewer when it is on screen.
    void zoomViewer(int op); // 0=in, 1=out, 2=fit, 3=actual

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
    void restoreLastSession();
    // P0: Directory-level back/forward history (independent of image history).
    void pushDirHistory(const QString &dir);
    void goDirBack();
    void goDirForward();

    // P0-3: metadata overlay — position and show the floating metadata panel.
    void showMetadataOverlay();
    void toggleMetadataOverlay();
    void copyCurrentImageToClipboard();
    void openQuickCompare();
    void openPreferences();     // F1 (M22): centralized Preferences dialog
    void applyPreferences();    // re-apply view/sort/slideshow after settings change
    void openAnalysisOverlay(); // F4 (M22): zebra / false-color / scopes dialog

    void keyPressEvent(QKeyEvent *event) override;

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    BreadcrumbBar *m_breadcrumb = nullptr;
    QLineEdit *m_pathEdit = nullptr;              // Path input bar above the gallery area
    QSplitter *m_mainSplitter = nullptr;          // P1-3: central layout splitter
    QSplitter *m_leftSplitter = nullptr;          // A-6.4: nav | tree | preview heights
    MetadataOverlay *m_metadataOverlay = nullptr; // M15: semi-transparent info overlay
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    MetadataPanel *m_metadataPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;
    BatchDialog *m_batchDialog = nullptr;
    PluginSettings *m_pluginSettings = nullptr;
    CompareWorkspace *m_compareView = nullptr;
    QMetaObject::Connection m_compareDestroyConnection; // guard WA_DeleteOnClose

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
    QAction *m_actBatch = nullptr;
    QAction *m_actPluginSettings = nullptr;
    QAction *m_actToggleMetadata = nullptr;
    QAction *m_actExportSettings = nullptr;
    QAction *m_actImportSettings = nullptr;
    // A-10: Undo/Redo via CommandStack.
    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    CommandStack m_cmdStack;
    void updateUndoRedoActions();
    void positionMetadataPanel();
    // Zoom / fullscreen view commands (forwarded to the image viewer).
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actZoomFit = nullptr;
    QAction *m_actZoomActual = nullptr;
    QAction *m_actFullscreen = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_recentFileMenu = nullptr; // recent-files menu (opened images)
    QMenu *m_favMenu = nullptr;

    // M15: crash recovery
    QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveLoaded = false;

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

    // M18: gallery search bar.
    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_searchRecursive = nullptr;
    // P1: metadata-aware search + star-rating filter.
    QCheckBox *m_searchMeta = nullptr;
    QComboBox *m_ratingFilter = nullptr;
    QComboBox *m_sortCombo = nullptr;     // persisted across sessions via QSettings
    QSlider *m_thumbSizeSlider = nullptr; // persisted across sessions via QSettings
    QComboBox *m_flagFilter = nullptr;    // P3 tail: color label / reject / pick / recents

    // P0 #①: real-time status bar (image count / size / zoom / cache hit-rate).
    QLabel *m_lblImage = nullptr; // current image dimensions + file size
    QLabel *m_lblCount = nullptr;
    QLabel *m_lblSize = nullptr;
    QLabel *m_lblZoom = nullptr;
    QLabel *m_lblCache = nullptr;
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
    // P0: Favorites bar widget in the left sidebar.
    QListWidget *m_favoritesBar = nullptr;
    // Persisted, cross-session app state (favorites + restore position).
    mviewer::core::RecentFiles m_recent;      // recent-folders LRU
    mviewer::core::RecentFiles m_recentFiles; // recent-files LRU (opened images)
    QString m_openOnLaunch;                   // path passed via command line
    AppState m_appState;

    void saveWorkspace();
    void openWorkspace();
    // M15 (Project): persist / restore the full evaluation environment as a
    // self-contained .mvproj file (datasets + compare session + analysis +
    // analyzer pipeline + export/review/benchmark config).
    void saveProject();
    void openProject();
    void exportReport();
    // P4: batch export pipeline entry point.
    void exportImages();
    // A-3: resolve paths for Export/Batch/Compare from SelectionModel first,
    // then fall back to gallery selection / visible set / full directory.
    QStringList resolveSelectedPaths(bool preferMulti = true) const;
    // A-3.4: enable/disable selection-dependent actions (Compare/Export/Batch).
    void updateSelectionActions();
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
    void checkForUpdates(bool silent = false);
    void onUpdateChecked(const mviewer::core::UpdateInfo &info, bool silent);
    void maybeShowCrashReport();
    bool m_updateChecking = false;

    // Shared drop handling for the main window and the thumbnail gallery:
    // ≥2 images → Compare; a directory → open folder; one image → open it.
    void handleDroppedPaths(const QStringList &paths);

    // Slideshow: auto-advance through the current folder on a timer.
    void toggleSlideshow();
    void stopSlideshow();
    QTimer *m_slideshowTimer = nullptr;
    QAction *m_actSlideshow = nullptr;
    bool m_dragHighlight = false; // border highlight while a drag is hovering
};
