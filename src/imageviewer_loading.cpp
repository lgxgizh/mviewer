#include "imageviewer.h"

#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace
{

constexpr int kDisplayWarmMaxEdge = 1024;
constexpr qint64 kDisplayWarmMaxBytes = 64LL * 1024 * 1024;
constexpr qint64 kDisplayWarmLodThresholdPixels = 16LL * 1000 * 1000;
constexpr size_t kDisplayWarmMaxEntries = 2;

} // namespace

void ImageViewer::setBrowseSequence(const QStringList &paths)
{
    if (paths != m_fileList)
    {
        ++m_displayRasterBrowseGeneration;
        cancelDisplayRasterPreloads();
        m_displayRasterWarm.clear();
        m_displayRasterWarmBytes = 0;
    }
    m_fileList = paths;
    m_currentIndex = m_fileList.indexOf(m_currentPath);

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
        m_currentIndex >= 0
            ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
            : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName())
                       .arg(size.width())
                       .arg(size.height())
                       .arg(position));
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
    QTimer::singleShot(0, this,
                       [this]
                       {
                           if (!isVisible())
                               return;
                           raise();
                           activateWindow();
                           setFocus(Qt::OtherFocusReason);
                       });
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
    try
    {
        setImageImpl(path);
    }
    catch (...)
    {
        // An unexpected error must never escape into the Qt event loop; the
        // viewer stays in its current state and the host can retry.
    }
}

void ImageViewer::refreshSource(const QString &path)
{
    if (path.isEmpty() || path != m_currentPath)
        return;
    // The source/cache invalidation is owned by the caller. Re-enter the
    // normal cancellable load path while retaining the user's zoom/pan.
    m_preserveViewOnReload = true;
    setImage(path);
    m_preserveViewOnReload = false;
}

void ImageViewer::renameBrowsePaths(const QStringList &oldPaths, const QStringList &newPaths)
{
    const int count = qMin(oldPaths.size(), newPaths.size());
    for (int i = 0; i < count; ++i)
    {
        const QString &oldPath = oldPaths.at(i);
        const QString &newPath = newPaths.at(i);
        for (QString &path : m_fileList)
            if (path == oldPath)
                path = newPath;
        if (m_currentPath == oldPath)
        {
            m_currentPath = newPath;
            m_provisionalPath = newPath;
        }
    }
    m_currentIndex = m_fileList.indexOf(m_currentPath);
    if (!m_currentPath.isEmpty())
    {
        const QString position = m_currentIndex >= 0
                                     ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                     : QString();
        setWindowTitle(QString("%1%2 - MViewer")
                           .arg(QFileInfo(m_currentPath).fileName(), position));
    }
}

void ImageViewer::setImageImpl(const QString &path)
{
    // Pixel Inspector lifecycle: invalidate synchronously on every new load so
    // the previous image's sample never lingers while the next decode runs —
    // including for empty/failing requests that never deliver a frame.
    clearPixelInfo();
    const std::optional<Viewport> reloadView = m_preserveViewOnReload
                                                   ? std::optional<Viewport>(m_view)
                                                   : std::nullopt;
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
    auto matchingDisplayPreload = takeMatchingDisplayRasterPreload(path);
    const uint64_t gen = ++m_requestGen;
    beginImageGeneration();
    m_tileCache.clear();
    m_overlayCache.clear();
    m_frame.reset();
    m_sequence = {};
    m_frameIndex = 0;
    m_playback = {};
    m_tiles = TileGrid();
    m_hasHistogram = false;
    m_loading = !path.isEmpty();
    m_pendingView.reset();
    if (reloadView)
        m_pendingView = reloadView;
    // M47: reset the LOD-first display state for the new request. Any
    // in-flight raster worker is cancelled; its (bounded) completion is
    // discarded by the generation guard. The matching preload (if any) is
    // retained for the raster-path verdict's analysis load decision.
    cancelDisplayRequest();
    m_lodMode = false;
    m_largeSourcePending = false;
    m_analysisLoadIssued = false;
    m_sourceImage.reset();
    m_raster = DisplayRaster{};
    m_pendingAnalysisPreload = std::move(matchingPreload);
    m_promotedDisplayRasterPreload = std::move(matchingDisplayPreload);
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
        m_pendingAnalysisPreload.reset();
        update();
        return;
    }
    // M47: the raster-path verdict decides the full-frame load. The probe
    // worker reports small-vs-large; small sources keep the existing fast
    // path, large feasible sources get the analysis-support full load, and
    // infeasible sources (> Qt's allocation limit) skip it until Phase 4's
    // explicit source materialization. The display never waits for it.
    startLodDisplay(path, gen);
}

// M47: issue the analysis-support full-frame load (promoting a matching
// neighbor preload when one was retained). Runs on the UI thread from the
// raster-path verdict; the load itself is async + cancellable + lifetime-safe
// exactly as the pre-M47 foreground load was.
void ImageViewer::issueAnalysisLoad(const QString &path, uint64_t generation)
{
    auto onLoaded = makeImageLoadCallback(path, generation);
    // Frame zero is an explicit request. Static-image preloads use the same
    // frame-aware cache key, but a stale promoted legacy handle must never
    // erase the requested frame identity.
    m_pendingAnalysisPreload.reset();
    m_foregroundRequest =
        mviewer::application::ImageLoadingService::instance().loadFrameAsync(
            path.toUtf8().toStdString(), 0, std::move(onLoaded),
            ImageRepository::kDefaultLoadOptions, m_lifetime);
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
                                  // M47: while the raster-path verdict for a
                                  // large source is still pending, suppress the
                                  // analysis-support full-frame failure (the
                                  // 100 MP full decode fails fast, the LOD
                                  // arrives later — the display owns the
                                  // verdict).
                                  if (viewer->m_largeSourcePending)
                                  {
                                      viewer->update();
                                      return;
                                  }
                                  // M47: in LOD-first display the image IS on
                                  // screen (the raster path); only the
                                  // analysis-support full frame failed (e.g.
                                  // > Qt's allocation limit). Do not clobber
                                  // the successful display with a failure.
                                  if (viewer->m_lodMode && !viewer->m_raster.image.isNull())
                                  {
                                      viewer->update();
                                      return;
                                  }
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
        // M47: a failing analysis-support full frame must not clobber a
        // successful LOD-first display (same guard as queueImageLoadFailure).
        if (m_lodMode && !m_raster.image.isNull())
        {
            update();
            return;
        }
        setWindowTitle(QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
        update();
        emit loadFailed(path);
        return;
    }

    m_sequence = m_frame->sequenceInfo();
    m_frameIndex = m_frame->frameIndex();
    m_playback.configure(m_sequence);
    m_playback.setFrameInfo({m_frameIndex, m_frame->metadata().frameDurationMs > 0
                                                  ? m_frame->metadata().frameDurationMs
                                                  : 100,
                             m_frame->width(), m_frame->height()});

    computeHistogram();
    const QFileInfo info(path);
    m_currentIndex = static_cast<int>(m_fileList.indexOf(path));
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    // M47: in LOD-first display the full frame is the analysis/Inspector
    // source only — the display keeps the bounded raster (no re-fit that would
    // drop the user's zoom, no UI-thread scaling of the full frame).
    if (m_lodMode)
    {
        preloadDisplayRasterNeighbors(path);
        update();
        emit imageReady(m_frame);
        return;
    }
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
    if (m_sequence.animated)
        play();
    else
        updateFramePresentationStatus();
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

void ImageViewer::preloadDisplayRasterNeighbors(const QString &path)
{
    if (m_currentIndex < 0 || path.isEmpty())
        return;

    cancelDisplayRasterPreloads();
    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size())
            continue;
        const QString neighbor = m_fileList[i];
        if (neighbor == path)
            continue;
        const auto alreadyWarm = std::find_if(
            m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
            [&](const DisplayRasterWarm &warm) { return warm.path == neighbor; });
        if (alreadyWarm != m_displayRasterWarm.end())
            continue;

        auto state = std::make_shared<DisplayRasterPreloadState>();
        auto guard = std::make_shared<QPointer<ImageViewer>>(this);
        const uint64_t browseGeneration = m_displayRasterBrowseGeneration;
        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Thumbnail,
            [neighbor, browseGeneration, state, guard](const TaskScheduler::TaskContext &ctx)
            {
                try
                {
                    runDisplayRasterPreload(neighbor, browseGeneration, state, ctx, guard);
                }
                catch (...)
                {
                    if (!ctx.isCancelled())
                    {
                        DisplayRasterPreloadResult result;
                        result.path = neighbor;
                        result.browseGeneration = browseGeneration;
                        result.state = state;
                        result.failed = true;
                        queueDisplayRasterPreloadResult(guard, std::move(result));
                    }
                }
            },
            {}, std::chrono::steady_clock::time_point::max(), [] {});
        if (handle)
            m_displayRasterPreloads.push_back({neighbor, std::move(state), std::move(handle)});
    }
}

void ImageViewer::cancelCurrentLoad()
{
    cancelFrameRequests();
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_foregroundRequest);
}

void ImageViewer::cancelPreloads()
{
    for (auto &p : m_neighborPreloads)
        mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    m_neighborPreloads.clear();
}

void ImageViewer::cancelDisplayRasterPreloads()
{
    for (auto &preload : m_displayRasterPreloads)
        TaskScheduler::cancel(preload.handle);
    m_displayRasterPreloads.clear();
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

ImageViewer::DisplayRasterPreload ImageViewer::takeMatchingDisplayRasterPreload(
    const QString &path)
{
    DisplayRasterPreload match;
    for (auto &preload : m_displayRasterPreloads)
    {
        if (!match.handle && preload.path == path)
            match = std::move(preload);
        else
            TaskScheduler::cancel(preload.handle);
    }
    m_displayRasterPreloads.clear();
    return match;
}

std::optional<ImageViewer::DisplayRasterWarm> ImageViewer::takeWarmDisplayRaster(
    const QString &path)
{
    const auto it = std::find_if(
        m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
        [&](const DisplayRasterWarm &warm) { return warm.path == path; });
    if (it == m_displayRasterWarm.end())
        return std::nullopt;
    DisplayRasterWarm warm = std::move(*it);
    m_displayRasterWarmBytes = std::max<qint64>(0, m_displayRasterWarmBytes - warm.bytes);
    m_displayRasterWarm.erase(it);
    ++m_displayRasterWarmHits;
    return warm;
}

void ImageViewer::enforceDisplayRasterWarmBudget()
{
    while (m_displayRasterWarm.size() > kDisplayWarmMaxEntries ||
           m_displayRasterWarmBytes > kDisplayWarmMaxBytes)
    {
        if (m_displayRasterWarm.empty())
            break;
        const auto oldest = std::min_element(
            m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
            [](const DisplayRasterWarm &a, const DisplayRasterWarm &b)
            { return a.lastUse < b.lastUse; });
        m_displayRasterWarmBytes =
            std::max<qint64>(0, m_displayRasterWarmBytes - oldest->bytes);
        m_displayRasterWarm.erase(oldest);
    }
}

void ImageViewer::storeWarmDisplayRaster(DisplayRasterPreloadResult result)
{
    if (result.image.isNull() || !result.source || result.path.isEmpty())
        return;
    const qint64 bytes = static_cast<qint64>(result.image.sizeInBytes());
    if (bytes <= 0 || bytes > kDisplayWarmMaxBytes)
        return;

    const auto old = std::find_if(
        m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
        [&](const DisplayRasterWarm &warm) { return warm.path == result.path; });
    if (old != m_displayRasterWarm.end())
    {
        m_displayRasterWarmBytes = std::max<qint64>(0, m_displayRasterWarmBytes - old->bytes);
        m_displayRasterWarm.erase(old);
    }
    m_displayRasterWarm.push_back({result.path, std::move(result.source), std::move(result.image),
                                   result.sourceRect, result.sourceSize, result.density, bytes,
                                   ++m_displayRasterWarmClock});
    m_displayRasterWarmBytes += bytes;
    enforceDisplayRasterWarmBudget();
}

void ImageViewer::runDisplayRasterPreload(
    const QString &path, uint64_t browseGeneration,
    const std::shared_ptr<DisplayRasterPreloadState> &state,
    const TaskScheduler::TaskContext &ctx, const std::shared_ptr<QPointer<ImageViewer>> &guard)
{
    if (ctx.isCancelled())
        return;
    DisplayRasterPreloadResult result;
    result.path = path;
    result.browseGeneration = browseGeneration;
    result.state = state;
    try
    {
        result.source = mviewer::core::SourceImage::open(path.toStdString());
        if (!result.source)
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        result.sourceSize = QSize(result.source->metadata().width, result.source->metadata().height);
        const qint64 pixels = static_cast<qint64>(result.sourceSize.width()) *
                              result.sourceSize.height();
        if (pixels <= kDisplayWarmLodThresholdPixels)
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        const auto raster = result.source->decodeLod(kDisplayWarmMaxEdge);
        if (!raster.ok || raster.pixels.isNull())
        {
            result.failed = true;
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        result.sourceRect = QRect(0, 0, result.sourceSize.width(), result.sourceSize.height());
        result.density = static_cast<double>(result.sourceSize.width()) / raster.pixels.width;
        result.image = mvcore::toDisplayQImage(raster.pixels, raster.metadata);
    }
    catch (...)
    {
        result.failed = true;
    }
    if (ctx.isCancelled())
        return;
    queueDisplayRasterPreloadResult(guard, std::move(result));
}

void ImageViewer::queueDisplayRasterPreloadResult(
    const std::shared_ptr<QPointer<ImageViewer>> &guard, DisplayRasterPreloadResult result)
{
    if (!qApp)
        return;
    QMetaObject::invokeMethod(
        qApp,
        [guard, result = std::move(result)]() mutable
        {
            try
            {
                ImageViewer *viewer = guard ? guard->data() : nullptr;
                if (viewer)
                    viewer->applyDisplayRasterPreloadResult(std::move(result));
            }
            catch (...)
            {
            }
        },
        Qt::QueuedConnection);
}

void ImageViewer::applyDisplayRasterPreloadResult(DisplayRasterPreloadResult result)
{
    if (result.state)
    {
        m_displayRasterPreloads.erase(
            std::remove_if(m_displayRasterPreloads.begin(), m_displayRasterPreloads.end(),
                           [&](const DisplayRasterPreload &preload)
                           { return preload.state == result.state; }),
            m_displayRasterPreloads.end());
    }
    const uint64_t promotedGeneration =
        result.state ? result.state->promotedGeneration.load(std::memory_order_acquire) : 0;
    if (promotedGeneration != 0)
    {
        if (result.path != m_currentPath || promotedGeneration != m_requestGen)
            return;
        m_displayRequest.reset();
        m_promotedDisplayRasterPreload = DisplayRasterPreload{};
        if (result.failed || result.image.isNull())
        {
            // The neighbor result is best effort. Re-enter the normal probe
            // path if it was promoted before the worker discovered a failure.
            startLodDisplay(result.path, promotedGeneration);
            return;
        }
        applyDisplayRaster(result.path, promotedGeneration, std::move(result.source),
                           std::move(result.image), result.sourceRect, result.sourceSize,
                           result.density, false);
        return;
    }

    if (result.browseGeneration != m_displayRasterBrowseGeneration || result.failed ||
        result.image.isNull())
        return;
    const bool stillListed = m_fileList.contains(result.path);
    if (stillListed)
        storeWarmDisplayRaster(std::move(result));
}
