#pragma once

#include "appstate.h"
#include "core/command/CommandRegistry.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <QKeyEvent>
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
class BatchDialog;
class QAction;
class QMenu;
class QLineEdit;
class QCheckBox;
class QLabel;
class QTimer;
class QComboBox;

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
    void setOpenOnLaunch(const QString &path) { m_openOnLaunch = path; }

  protected:
    void closeEvent(QCloseEvent *event) override;
    // M15: drag & drop support
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    void setupCommands();
    void openCompare(const QStringList &images = {}, const QString &sessionJson = {});
    void navigate(int delta);
    void onBreadcrumbPath(const QString &path);

    // P0: product browse state — recent folders, favorites, in-session history,
    // and cross-session restore.
    void pushHistory(const QString &path);
    void navigateHistory(int delta);
    void rebuildRecentMenu();
    void rebuildRecentFilesMenu();
    void rebuildFavoritesMenu();
    void addFavoriteCurrent();
    void restoreLastSession();

    // P1: metadata overlay — position and show the floating metadata panel.
    void showMetadataOverlay();

    void keyPressEvent(QKeyEvent *event) override;

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    BreadcrumbBar *m_breadcrumb = nullptr;
    MetadataOverlay *m_metadataOverlay = nullptr;  // M15: semi-transparent info overlay
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    MetadataPanel *m_metadataPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;
    BatchDialog *m_batchDialog = nullptr;
    CompareWorkspace *m_compareView = nullptr;
    QMetaObject::Connection m_compareDestroyConnection;  // guard WA_DeleteOnClose

    QAction *m_actOpenDir = nullptr;
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
    QAction *m_actHistoryBack = nullptr;
    QAction *m_actHistoryForward = nullptr;
    QAction *m_actToggleSearch = nullptr;
    QAction *m_actBatch = nullptr;
    QAction *m_actToggleMetadata = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_recentFileMenu = nullptr;  // recent-files menu (opened images)
    QMenu *m_favMenu = nullptr;

    // M15: crash recovery
    QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveLoaded = false;

    // M15 Sprint 2-1: global search index rebuild on directory change.
    void reindexSearch();

    // M18: gallery search bar.
    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_searchRecursive = nullptr;
    // P1: metadata-aware search + star-rating filter.
    QCheckBox *m_searchMeta = nullptr;
    QComboBox *m_ratingFilter = nullptr;
    QComboBox *m_flagFilter = nullptr;  // P3 tail: color label / reject / pick / recents

    // P0 #①: real-time status bar (image count / size / zoom / cache hit-rate).
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

    QString m_currentDir;
    QString m_currentImagePath;
    QStringList m_cachedImagePaths; // cached image list for current dir
    bool m_dirListDirty = true;     // invalidated when directory changes

    // In-session navigation history (like a browser back/forward).
    QStringList m_history;
    int m_historyIndex = -1;
    // Persisted, cross-session app state (favorites + restore position).
    mviewer::core::RecentFiles m_recent;         // recent-folders LRU
    mviewer::core::RecentFiles m_recentFiles;    // recent-files LRU (opened images)
    QString m_openOnLaunch;       // path passed via command line
    AppState m_appState;

    // M12.2 (G2-ext): per-image last analysis result text, keyed by image path.
    // Populated as analysis runs for each opened image; persisted per-image into
    // the .mvws so a compare session's analysis context survives a reload.
    QMap<QString, QString> m_analysisByPath;

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
    // M15: crash recovery
    void autosaveSession();
    void restoreSessionRecovery();
};
