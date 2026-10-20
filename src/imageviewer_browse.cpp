// ImageViewer browse-sequence bookkeeping: the gallery order, window title
// position and browse fullscreen entry. Split from imageviewer_loading.cpp
// for complexity-gate line headroom; behavior is unchanged.
#include "imageviewer.h"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>

void ImageViewer::setBrowseSequence(const QStringList &paths)
{
    if (paths != m_fileList)
    {
        ++m_displayRasterBrowseGeneration;
        cancelDisplayRasterPreloads();
        m_displayRasterWarm.clear();
        m_displayRasterWarmBytes = 0;
        m_fileIndex.invalidate();
    }
    m_fileList = paths;
    m_currentIndex = indexInBrowseSequence(m_currentPath);

    if (m_currentPath.isEmpty())
        return;

    if (m_currentIndex >= 0)
    {
        if (m_lodMode || m_largeSourcePending)
            preloadDisplayRasterNeighbors(m_currentPath);
        else if (m_frame)
            preloadNeighbors(m_currentPath);
    }
    else
    {
        cancelPreloads();
        cancelDisplayRasterPreloads();
    }

    const QSize size = displaySize();
    if (!size.isValid())
        return;
    const QFileInfo info(m_currentPath);
    const QString position =
        m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                            : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName(), QString::number(size.width()),
                            QString::number(size.height()), position));
}

void ImageViewer::showBrowseFullscreen()
{
    setFullscreenRequested(true);
    raise();
    activateWindow();
    // showFullScreen() can preserve the previous gallery focus on some window
    // managers. The browse entry point must finish with the Viewer focused so
    // the first ESC always closes it and viewer-owned keys respond immediately.
    setFocus(Qt::OtherFocusReason);
    QApplication::setActiveWindow(this);
    QTimer::singleShot(0, this,
                       [this]
                       {
                           if (!isVisible())
                               return;
                           raise();
                           activateWindow();
                           setFocus(Qt::OtherFocusReason);
                           QApplication::setActiveWindow(this);
                       });
}

void ImageViewer::renameBrowsePaths(const QStringList &oldPaths, const QStringList &newPaths)
{
    const int count = qMin(oldPaths.size(), newPaths.size());
    for (int i = 0; i < count; ++i)
    {
        const QString &oldPath = oldPaths.at(i);
        const QString &newPath = newPaths.at(i);
        for (QString &path : m_fileList)
            if (browsePathEquals(path, oldPath))
                path = newPath;
        if (browsePathEquals(m_currentPath, oldPath))
        {
            m_currentPath = newPath;
            m_provisionalPath = newPath;
        }
    }
    m_currentIndex = indexInBrowseSequence(m_currentPath);
    if (!m_currentPath.isEmpty())
    {
        const QString position =
            m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                : QString();
        setWindowTitle(
            QString("%1%2 - MViewer").arg(QFileInfo(m_currentPath).fileName(), position));
    }
}
