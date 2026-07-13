#pragma once

#include <QMainWindow>

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

private:
    void setupUi();
    void openCompare(const QStringList &images = {});
    void onImageOpen(const QString &path);
    void navigate(int delta);

    ImageViewer *m_imageViewer = nullptr;
    DirectoryTree *m_directoryTree = nullptr;
    ThumbnailPanel *m_thumbnailPanel = nullptr;
    PreviewPanel *m_previewPanel = nullptr;

    AnalysisPanel *m_analysisPanel = nullptr;
    CompareWorkspace *m_compareView = nullptr;

    QAction *m_actOpenDir = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actCompare = nullptr;
    QAction *m_actToggleAnalysis = nullptr;
    QAction *m_actAbout = nullptr;

    QString m_currentDir;
    QString m_currentImagePath;
};
