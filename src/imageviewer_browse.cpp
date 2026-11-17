// ImageViewer browse-sequence bookkeeping: the gallery order, window title
// position and browse fullscreen entry. Split from imageviewer_loading.cpp
// for complexity-gate line headroom. Append-only growth keeps neighbor preloads.
#include "imageviewer.h"

#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <memory>

namespace
{

// True when `incoming` keeps every current path, in order, and only adds a suffix.
bool browseSequenceExtends(const QStringList &current, const QStringList &incoming)
{
    if (incoming.size() <= current.size())
        return false;
    for (int i = 0; i < current.size(); ++i)
    {
        if (current.at(i) == incoming.at(i))
            continue;
        if (!ImageViewer::browsePathEquals(current.at(i), incoming.at(i)))
            return false;
    }
    return true;
}

} // namespace

void ImageViewer::invalidateDisplayRasterBrowse()
{
    m_displayRasterRetain.evictions += static_cast<uint64_t>(m_displayRasterWarm.size());
    ++m_displayRasterBrowseGeneration;
    cancelDisplayRasterPreloads();
    m_displayRasterWarm.clear();
    m_displayRasterWarmBytes = 0;
}

ImageViewer::DisplayRasterRetainStats ImageViewer::displayRasterRetainStats() const
{
    DisplayRasterRetainStats stats = m_displayRasterRetain;
    stats.inFlight = static_cast<uint64_t>(m_displayRasterPreloads.size());
    return stats;
}

void ImageViewer::resetDisplayRasterRetainStats()
{
    m_displayRasterRetain = {};
}

void ImageViewer::setBrowseSequence(const QStringList &paths)
{
    if (paths != m_fileList)
    {
        // Progressive scan batches are a suffix append. Keep the browse
        // generation, in-flight neighbor preloads, and warm rasters so the
        // preload pass below can retain still-adjacent work. Reorder, removal,
        // filter shrink, and replacement still drop that presentation cache.
        // The current image's display request is not cancelled on either path.
        if (!browseSequenceExtends(m_fileList, paths))
            invalidateDisplayRasterBrowse();
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

QAction *ImageViewer::addBrowseContextActions(QMenu &menu, QAction *&next, QAction *&previous)
{
    next = menu.addAction(tr("Next (→)"));
    previous = menu.addAction(tr("Previous (←)"));
    const bool hasBrowsePosition = m_currentIndex >= 0 && m_currentIndex < m_fileList.size();
    next->setEnabled(hasBrowsePosition && m_currentIndex + 1 < m_fileList.size());
    previous->setEnabled(hasBrowsePosition && m_currentIndex > 0);
    menu.addSeparator();
    const bool isFull = property("mviewerFullscreenRequested").toBool() || isFullScreen();
    return menu.addAction(isFull ? tr("Exit Fullscreen (F11)") : tr("Fullscreen (F11)"));
}

void ImageViewer::prepareBrowseFullscreen(QScreen *targetScreen)
{
    if (!property("mviewerFullscreenRequested").toBool() && !isFullScreen() &&
        !windowState().testFlag(Qt::WindowFullScreen))
        m_windowedGeometry = saveGeometry();
    setProperty("mviewerFullscreenRequested", true);
    if (targetScreen)
    {
        if (screen() != targetScreen)
            setScreen(targetScreen);
        const QRect geom = targetScreen->geometry();
        setGeometry(geom);
        m_view.screenW = geom.width();
        m_view.screenH = geom.height();
    }
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

void ImageViewer::preloadDisplayRasterNeighbors(const QString &path)
{
    if (m_currentIndex < 0 || path.isEmpty())
        return;
    std::vector<QString> needed;
    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size() || browsePathEquals(m_fileList.at(i), path))
            continue;
        const QString neighbor = m_fileList[i];
        if (std::any_of(m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
                        [&](const DisplayRasterWarm &w) { return w.path == neighbor; }))
            continue;
        needed.push_back(neighbor);
    }
    std::vector<DisplayRasterPreload> kept; // still-adjacent in-flight preloads
    for (auto &preload : m_displayRasterPreloads)
    {
        auto it = std::find_if(needed.begin(), needed.end(),
                               [&](const QString &n) { return browsePathEquals(preload.path, n); });
        const bool usable =
            it != needed.end() && preload.handle && !preload.handle->isCancelled() &&
            !(preload.state && preload.state->delivered.load(std::memory_order_acquire));
        if (usable)
        {
            needed.erase(it);
            kept.push_back(std::move(preload));
        }
        else
        {
            ++m_displayRasterRetain.cancels;
            TaskScheduler::cancel(preload.handle);
        }
    }
    m_displayRasterPreloads = std::move(kept);
    for (const QString &neighbor : needed)
    {
        auto state = std::make_shared<DisplayRasterPreloadState>();
        state->widgetW = width();
        state->widgetH = height();
        state->dpr = std::max(1.0, devicePixelRatioF());
        if (m_lockZoom && !m_fitMode && m_view.scale > 0.0)
            state->lockedScale = m_view.scale;
        state->maximizeFit = property("mviewerFullscreenRequested").toBool();
        auto guard = std::make_shared<QPointer<ImageViewer>>(this);
        const uint64_t browseGen = m_displayRasterBrowseGeneration;
        const auto target = m_displayColorTarget;
        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Thumbnail,
            [neighbor, browseGen, state, guard, target](const TaskScheduler::TaskContext &ctx)
            {
                try
                {
                    runDisplayRasterPreload(neighbor, browseGen, state, target, ctx, guard);
                }
                catch (...)
                {
                    const uint64_t promoted =
                        state ? state->promotedGeneration.load(std::memory_order_acquire) : 0;
                    if (!ctx.isCancelled() || promoted != 0)
                    {
                        DisplayRasterPreloadResult res;
                        res.path = neighbor;
                        res.browseGeneration = browseGen;
                        res.state = state;
                        res.target = target;
                        res.failed = true;
                        queueDisplayRasterPreloadResult(guard, std::move(res));
                    }
                }
            },
            {}, std::chrono::steady_clock::time_point::max(), [] {});
        if (handle)
        {
            m_displayRasterPreloads.push_back({neighbor, std::move(state), std::move(handle)});
            ++m_displayRasterRetain.starts;
        }
    }
}
