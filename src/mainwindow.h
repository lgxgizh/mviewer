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
class ThumbnailPanel;
class PreviewPanel;
class AnalysisPanel;
class MetadataPanel;
class CompareWorkspace;
class QAction;
class QMenu;
class QLineEdit;
class QCheckBox;

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

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void setupCommands();
    void openCompare(const QStringList &images = {});
    void navigate(int delta);

    // P0: product browse state — recent folders, favorites, in-session history,
    // and cross-session restore.
    void pushHistory(const QString &path);
    void navigateHistory(int delta);
    void rebuildRecentMenu();
    void rebuildFavoritesMenu();
    void addFavoriteCurrent();
    void restoreLastSession();

    void keyPressEvent(QKeyEvent *event) override;

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    MetadataPanel *m_metadataPanel = nullptr;
    CompareWorkspace *m_compareView = nullptr;

    QAction *m_actOpenDir = nullptr;
    QAction *m_actSaveWorkspace = nullptr;
    QAction *m_actOpenWorkspace = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actCompare = nullptr;
    QAction *m_actToggleAnalysis = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actAddFavorite = nullptr;
    QAction *m_actHistoryBack = nullptr;
    QAction *m_actHistoryForward = nullptr;

    QMenu *m_recentMenu = nullptr;
    QMenu *m_favMenu = nullptr;

    // M18: gallery search bar.
    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_searchRecursive = nullptr;

    QString m_currentDir;
    QString m_currentImagePath;
    QStringList m_cachedImagePaths; // cached image list for current dir
    bool m_dirListDirty = true;     // invalidated when directory changes

    // In-session navigation history (like a browser back/forward).
    QStringList m_history;
    int m_historyIndex = -1;

    // Persisted, cross-session app state (favorites + restore position).
    AppState m_appState;
    mviewer::core::RecentFiles m_recent;

    // M12.2 (G2-ext): per-image last analysis result text, keyed by image path.
    // Populated as analysis runs for each opened image; persisted per-image into
    // the .mvws so a compare session's analysis context survives a reload.
    QMap<QString, QString> m_analysisByPath;

    void saveWorkspace();
    void openWorkspace();
};
