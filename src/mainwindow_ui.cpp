// MainWindow UI construction: widgets, menus, docks, status bar (M20 P0#1).
#include "mainwindow_p.h"

#include <QSignalBlocker>
#include <QStyle>
#include <QToolBar>

void MainWindow::setupUi()
{
    buildMenus();
    buildBrowserShell();
    QWidget *leftWidget = buildNavigationPanel();
    QWidget *rightWidget = buildGalleryPanel();
    buildAnalysisAndSearchPanels();
    buildCentralContainer(leftWidget, rightWidget);
    buildMetadataPanelUi();
    buildImageViewerUi();
    connectUiSignals();
    buildStatusBarUi();
}

void MainWindow::updateEmptyState()
{
    if (!m_emptyState || !m_thumbnailPanel)
        return;
    const bool show = currentDir().isEmpty();
    m_emptyState->setVisible(show);
    if (show)
        m_emptyState->setGeometry(m_thumbnailPanel->rect());
}

void MainWindow::updateEmptyFolderState()
{
    if (!m_emptyFolderLabel || !m_thumbnailPanel)
        return;
    const bool show = !currentDir().isEmpty() && m_thumbnailPanel->pathList().isEmpty();
    m_emptyFolderLabel->setVisible(show);
    if (show)
        m_emptyFolderLabel->setGeometry(m_thumbnailPanel->rect());
}
