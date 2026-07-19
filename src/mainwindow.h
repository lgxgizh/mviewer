#pragma once

#include "core/command/CommandRegistry.h"

#include <QKeyEvent>
#include <QMainWindow>
#include <QMap>

class ImageViewer;
class DirectoryTree;
class ThumbnailPanel;
class PreviewPanel;
class AnalysisPanel;
class CompareWorkspace;
class QAction;

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

  private:
    void setupCommands();
    void openCompare(const QStringList &images = {});
    void navigate(int delta);

    void keyPressEvent(QKeyEvent *event) override;

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    CompareWorkspace *m_compareView = nullptr;

    QAction *m_actOpenDir = nullptr;
    QAction *m_actSaveWorkspace = nullptr;
    QAction *m_actOpenWorkspace = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actCompare = nullptr;
    QAction *m_actToggleAnalysis = nullptr;
    QAction *m_actAbout = nullptr;

    QString m_currentDir;
    QString m_currentImagePath;
    QStringList m_cachedImagePaths; // cached image list for current dir
    bool m_dirListDirty = true;     // invalidated when directory changes

    // M12.2 (G2-ext): per-image last analysis result text, keyed by image path.
    // Populated as analysis runs for each opened image; persisted per-image into
    // the .mvws so a compare session's analysis context survives a reload.
    QMap<QString, QString> m_analysisByPath;

    void saveWorkspace();
    void openWorkspace();
};
