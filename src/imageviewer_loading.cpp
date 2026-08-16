#include "imageviewer.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

void ImageViewer::setBrowseSequence(const QStringList &paths)
{
    m_fileList = paths;
    m_currentIndex = m_fileList.indexOf(m_currentPath);

    if (!m_frame || m_currentPath.isEmpty())
        return;

    if (m_currentIndex >= 0)
        preloadNeighbors(m_currentPath);
    else
        cancelPreloads();

    const QFileInfo info(m_currentPath);
    const QString position =
        m_currentIndex >= 0
            ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
            : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName())
                       .arg(m_frame->width())
                       .arg(m_frame->height())
                       .arg(position));
}

void ImageViewer::showBrowseFullscreen()
{
    setFullscreenRequested(true);
    raise();
    activateWindow();
}

void ImageViewer::setFullscreenRequested(bool requested)
{
    // This property is authoritative even when the offscreen platform cannot
    // report a reliable native isFullScreen() value.
    setProperty("mviewerFullscreenRequested", requested);
    if (requested)
    {
        setWindowState(windowState() | Qt::WindowFullScreen);
        showFullScreen();
    }
    else
    {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
        showNormal();
    }

    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    QTimer::singleShot(0, this,
                       [guard, requested]()
                       {
                           ImageViewer *viewer = guard ? guard->data() : nullptr;
                           if (!viewer)
                               return;
                           if (viewer->m_fitMode)
                           {
                               if (viewer->m_frame && viewer->m_frame->isValid())
                                   viewer->fitToWidget();
                               else if (!viewer->m_provisionalImage.isNull())
                               {
                                   viewer->m_view.screenW = viewer->width();
                                   viewer->m_view.screenH = viewer->height();
                                   const QSize source = viewer->m_provisionalSourceSize.isValid()
                                                             ? viewer->m_provisionalSourceSize
                                                             : viewer->m_provisionalImage.size();
                                   viewer->m_view.fit(source.width(), source.height(),
                                                     requested ? FitPolicy::MaximizeClient
                                                               : FitPolicy::Comfortable);
                                   viewer->advanceViewportRevision();
                                   viewer->emitZoom();
                               }
                           }
                           viewer->update();
                       });
}

void ImageViewer::setImage(const QString &path)
{
    // Pixel Inspector lifecycle: invalidate synchronously on every new load so
    // the previous image's sample never lingers while the next decode runs —
    // including for empty/failing requests that never deliver a frame.
    clearPixelInfo();
    const bool keepProvisional = !path.isEmpty() && path == m_provisionalPath &&
                                 !m_provisionalImage.isNull();
    m_currentPath = path;
    m_currentIndex = m_fileList.indexOf(path);
    // M29: drop the prior foreground decode BEFORE scheduling the new load, and
    // consume any neighbor preload that already targets `path` so it can be
    // promoted to the foreground decode below. Every nonmatching preload is
    // soft-cancelled here too, so obsolete queued Background work from the
    // previous navigation is skipped before it wastes CPU/I/O or re-warms the
    // cache for a superseded image.
    cancelCurrentLoad();
    auto matchingPreload = takeMatchingPreload(path);
    const uint64_t gen = ++m_requestGen;
    beginImageGeneration();
    m_tileCache.clear();
    m_overlayCache.clear();
    m_frame.reset();
    m_tiles = TileGrid();
    m_hasHistogram = false;
    m_loading = !path.isEmpty();
    m_pendingView.reset();
    if (!keepProvisional)
    {
        m_provisionalPath.clear();
        m_provisionalImage = QImage();
        m_provisionalSourceSize = QSize();
    }
    if (context())
    {
        makeCurrent();
        m_gpu.clear();
        doneCurrent();
    }
    if (path.isEmpty())
    {
        update();
        return;
    }
    auto onLoaded = makeImageLoadCallback(path, gen);
    if (matchingPreload)
        m_foregroundRequest =
            mviewer::application::ImageLoadingService::instance().promotePreloadAsync(
                matchingPreload, std::move(onLoaded), m_lifetime);
    else
        m_foregroundRequest =
            mviewer::application::ImageLoadingService::instance().loadAsyncCancellable(
                path.toUtf8().toStdString(), std::move(onLoaded), ImageRepository::kDefaultLoadOptions,
                m_lifetime);
}

ImageViewer::ImageLoadCallback ImageViewer::makeImageLoadCallback(const QString &path,
                                                                  uint64_t generation)
{
    // M46 strict lifetime contract: the completion lambda captures NO raw
    // `this` — only the QPointer guard (which is dereferenced exclusively on
    // the UI thread inside queueLoadedImage/queueImageLoadFailure), the path,
    // the generation and the shared lifetime token. The repository itself
    // already refuses to invoke this callback once the token is dead, so a
    // late completion is a no-op before any viewer-visible code runs.
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    return [path, generation, guard](const ImageLoadResult &result)
    {
        if (!guard)
            return;
        if (result.success())
            queueLoadedImage(path, generation, guard, result);
        else
            queueImageLoadFailure(path, generation, guard);
    };
}

void ImageViewer::queueImageLoadFailure(const QString &path, uint64_t generation,
                                        const ImageLoadGuard &guard)
{
    QMetaObject::invokeMethod(qApp,
                              [path, generation, guard]()
                              {
                                  ImageViewer *viewer = guard->data();
                                  if (!viewer || path != viewer->m_currentPath ||
                                      generation != viewer->m_requestGen)
                                      return;
                                  viewer->m_foregroundRequest.reset();
                                  viewer->m_loading = false;
                                  viewer->m_hasHistogram = false;
                                  viewer->setWindowTitle(
                                      QString("无法加载 - %1 - MViewer")
                                          .arg(QFileInfo(path).fileName()));
                                  viewer->update();
                                  emit viewer->loadFailed(path);
                              });
}

void ImageViewer::queueLoadedImage(const QString &path, uint64_t generation,
                                   const ImageLoadGuard &guard,
                                   const ImageLoadResult &result)
{
    QMetaObject::invokeMethod(qApp,
                              [path, generation, guard, result]()
                              {
                                  ImageViewer *viewer = guard->data();
                                  if (!viewer || path != viewer->m_currentPath ||
                                      generation != viewer->m_requestGen)
                                      return;
                                  viewer->applyLoadedImage(path, result);
                                  viewer->scheduleLoadedRefit(path, generation, guard);
                              });
}

void ImageViewer::applyLoadedImage(const QString &path, const ImageLoadResult &result)
{
    m_foregroundRequest.reset();
    m_loading = false;
    m_frame = result.frame;
    if (!m_frame || m_frame->pixels().isNull())
    {
        m_hasHistogram = false;
        setWindowTitle(QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
        update();
        emit loadFailed(path);
        return;
    }

    computeHistogram();
    const QFileInfo info(path);
    m_currentIndex = static_cast<int>(m_fileList.indexOf(path));
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    m_view.screenW = width();
    m_view.screenH = height();
    const FitPolicy fitPolicy = property("mviewerFullscreenRequested").toBool()
                                    ? FitPolicy::MaximizeClient
                                    : FitPolicy::Comfortable;
    m_view.fit(m_frame->width(), m_frame->height(), fitPolicy);
    m_fitMode = true;
    const QString position = m_currentIndex >= 0
                                 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                 : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName())
                       .arg(m_frame->width())
                       .arg(m_frame->height())
                       .arg(position));
    applyPendingView();
    m_overlayCache.clear();
    clearLoadedGpu();
    preloadNeighbors(path);
    update();
    emit imageReady(m_frame);
}

void ImageViewer::applyPendingView()
{
    if (!m_pendingView)
        return;
    m_view.scale = m_pendingView->scale;
    if (std::fabs(m_view.screenW - m_pendingView->screenW) < 2.0 &&
        std::fabs(m_view.screenH - m_pendingView->screenH) < 2.0)
    {
        m_view.offsetX = m_pendingView->offsetX;
        m_view.offsetY = m_pendingView->offsetY;
    }
    m_pendingView.reset();
    m_fitMode = false;
    emit zoomChanged(static_cast<int>(m_view.scale * 100.0 + 0.5));
}

void ImageViewer::clearLoadedGpu()
{
    if (!context())
        return;
    makeCurrent();
    m_gpu.clear();
    doneCurrent();
}

void ImageViewer::scheduleLoadedRefit(const QString &path, uint64_t generation,
                                      const ImageLoadGuard &guard)
{
    QTimer::singleShot(0, this,
                       [guard, path, generation]()
                       {
                           ImageViewer *viewer = guard->data();
                           if (!viewer || viewer->m_currentPath != path ||
                               viewer->m_requestGen != generation)
                               return;
                           if (viewer->property("mviewerFullscreenRequested").toBool() &&
                               viewer->m_fitMode)
                               viewer->fitToWidget();
                           viewer->update();
                       });
}

void ImageViewer::setViewTransform(const Viewport &v)
{
    // Store until the async load of the current image completes; the callback
    // in setImage() applies it once screen geometry is known.
    m_pendingView = v;
}

void ImageViewer::preloadNeighbors(const QString &path)
{
    if (m_currentIndex < 0)
        return;

    // M29: drop any preloads still tracked from the previous navigation so the
    // vector holds at most the previous/next pair of the CURRENT image.
    cancelPreloads();

    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size())
            continue;
        if (m_fileList[i] == path)
            continue;
        // Warm the cache only (off UI thread, Background priority, no histogram).
        // Do NOT call loadPixmap(): it assigns m_frame, which would race with
        // the UI thread's frame() read. Best-effort: a queued preload may be
        // skipped by the next navigation; a running decode may finish and
        // safely warm the cache. At most two handles are retained, each bound
        // to the path it prefetches so a navigation back to a neighbor can
        // promote this handle to the foreground decode (takeMatchingPreload).
        mviewer::application::ImageLoadingService::AsyncRequestHandle h =
            mviewer::application::ImageLoadingService::instance().preloadAsync(
                m_fileList[i].toUtf8().toStdString(), m_lifetime);
        if (h)
            m_neighborPreloads.push_back({m_fileList[i], std::move(h)});
    }
}

void ImageViewer::cancelCurrentLoad()
{
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_foregroundRequest);
}

void ImageViewer::cancelPreloads()
{
    for (auto &p : m_neighborPreloads)
        mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    m_neighborPreloads.clear();
}

mviewer::application::ImageLoadingService::AsyncRequestHandle
ImageViewer::takeMatchingPreload(const QString &path)
{
    // Consume the first tracked preload that exactly matches `path` and cancel
    // every other tracked preload, so a navigation back to a preloaded neighbor
    // can be promoted to the foreground decode instead of being re-queued. The
    // vector is emptied; at most one match is ever retained.
    mviewer::application::ImageLoadingService::AsyncRequestHandle match;
    for (auto &p : m_neighborPreloads)
    {
        if (!match && p.path == path)
            match = std::move(p.handle);
        else
            mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    }
    m_neighborPreloads.clear();
    return match;
}

