#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageRepository.h"
#include "core/image/ImageStats.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/trace/Trace.h"
#include "gpu/GpuTileUploader.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTextureBlitter>
#include <QPainter>
#include <QPointer>
#include <QRect>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

const double kZoomStep = 1.15;

ImageViewer::ImageViewer(QWidget *parent)
    : QOpenGLWidget(parent), m_tileRequests(m_tileCache), m_overlayRequests(m_overlayCache)
{
    setWindowTitle("图片查看");
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(200, 200);
    // Stage A: QPainter over GL FBO for overlays (histogram / selection).
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    // Restore window geometry from the last session.
    QSettings settings;
    m_overlayMode =
        static_cast<mviewer::OverlayMode>(settings.value("defaultAnalysisOverlay", 0).toInt());
    m_zebraThreshold = settings.value("zebraThreshold", 2).toInt();
    const QByteArray geom = settings.value("viewerGeometry").toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    else
        resize(900, 700);
    // Auto-hide cursor in fullscreen after 2.5s of inactivity.
    m_cursorHideTimer = new QTimer(this);
    m_cursorHideTimer->setSingleShot(true);
    m_cursorHideTimer->setInterval(2500);
    connect(m_cursorHideTimer, &QTimer::timeout, this,
            [this]()
            {
                if (isFullScreen() && !m_dragging && !m_selecting)
                {
                    m_cursorHidden = true;
                    setCursor(Qt::BlankCursor);
                }
            });
}

ImageViewer::~ImageViewer()
{
    // M29: drop any in-flight foreground decode / neighbor preload before
    // tearing down the GL context. A worker callback that lands after teardown
    // is harmless (the QPointer/path/generation guards suppress delivery), but
    // cancelling first also removes the wasted full-resolution decode work.
    cancelCurrentLoad();
    cancelPreloads();
    cancelRoiStats();
    // Stage A: free GPU textures + blitter while the GL context is still current.
    // Skip if the widget never created a context (never shown).
    if (context())
    {
        makeCurrent();
        m_gpu.clear();
        if (m_blitterReady)
        {
            m_blitter.destroy();
            m_blitterReady = false;
        }
        doneCurrent();
    }
}

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
    // Set the native state before showing the top-level widget. The property is
    // also the deterministic contract for headless/offscreen Qt platforms,
    // which may report isFullScreen() as false after the request.
    setProperty("mviewerFullscreenRequested", true);
    setWindowState(windowState() | Qt::WindowFullScreen);
    showFullScreen();
    raise();
    activateWindow();

    QPointer<ImageViewer> guard(this);
    QTimer::singleShot(0, this,
                       [guard]()
                       {
                           if (!guard)
                               return;
                           if (guard->isFullScreen() && guard->m_fitMode)
                           {
                               if (guard->m_frame && guard->m_frame->isValid())
                                   guard->fitToWidget();
                               else if (!guard->m_provisionalImage.isNull())
                               {
                                   guard->m_view.screenW = guard->width();
                                   guard->m_view.screenH = guard->height();
                                   const QSize source = guard->m_provisionalSourceSize.isValid()
                                                             ? guard->m_provisionalSourceSize
                                                             : guard->m_provisionalImage.size();
                                   guard->m_view.fit(source.width(), source.height(),
                                                     FitPolicy::MaximizeClient);
                                   guard->advanceViewportRevision();
                                   guard->emitZoom();
                               }
                           }
                           guard->update();
                       });
}

void ImageViewer::setProvisionalImage(const QString &path, const QImage &image,
                                      const QSize &sourceSize)
{
    if (path.isEmpty() || image.isNull())
        return;
    if (m_currentPath != path)
        beginImageGeneration();
    m_currentPath = path;
    m_provisionalPath = path;
    m_provisionalImage = image;
    m_provisionalSourceSize = sourceSize.isValid() ? sourceSize : image.size();
    m_view.screenW = width();
    m_view.screenH = height();
    const FitPolicy fitPolicy =
        isFullScreen() || property("mviewerFullscreenRequested").toBool()
            ? FitPolicy::MaximizeClient
            : FitPolicy::Comfortable;
    m_view.fit(m_provisionalSourceSize.width(), m_provisionalSourceSize.height(), fitPolicy);
    advanceViewportRevision();
    m_fitMode = true;
    emitZoom();
    update();
}

void ImageViewer::initializeGL()
{
    // Context is now current — capability probe can succeed when MVIEWER_GPU=1.
    // Stage A composites uploaded textures via QOpenGLTextureBlitter (Qt 6 has
    // no QPainter::drawTexture).
    if (QOpenGLFunctions *gl = QOpenGLContext::currentContext()
                                   ? QOpenGLContext::currentContext()->functions()
                                   : nullptr)
    {
        gl->glClearColor(0.f, 0.f, 0.f, 1.f);
    }
    m_blitterReady = m_blitter.create();
}

void ImageViewer::closeEvent(QCloseEvent *event)
{
    // Pixel Inspector lifecycle: drop any stale sample before the decode work
    // is cancelled and the window goes away.
    clearPixelInfo();
    // M29: stop stale decode/preload work before persisting geometry — a decode
    // callback already in flight must not repopulate preloads after close. The
    // QPointer/path/generation guards would still suppress UI delivery, but the
    // obsolete full-resolution work is dropped here.
    ++m_requestGen;
    beginImageGeneration();
    cancelCurrentLoad();
    cancelPreloads();
    QSettings settings;
    settings.setValue("viewerGeometry", saveGeometry());
    event->accept();
    emit viewerClosed();
}

void ImageViewer::leaveEvent(QEvent *event)
{
    // Pixel Inspector lifecycle: the cursor left the view, so drop the hovered
    // sample. Base first (default no-op), then invalidate.
    QOpenGLWidget::leaveEvent(event);
    clearPixelInfo();
}

void ImageViewer::clearPixelInfo()
{
    emit pixelInfo(-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, false);
}

void ImageViewer::cancelRoiStats()
{
    ++m_roiRevision;
    TaskScheduler::cancel(m_roiStatsRequest);
    m_roiStatsRequest.reset();
}

void ImageViewer::scheduleRoiStats(const QRect &selection)
{
    cancelRoiStats();
    if (!m_frame || selection.isEmpty())
        return;

    const auto frame = m_frame;
    const auto path = m_currentPath;
    const uint64_t revision = m_roiRevision;
    const mviewer::domain::Selection region{selection.x(), selection.y(), selection.width(),
                                             selection.height()};
    const auto result = std::make_shared<mviewer::core::PreviewStats>();
    QPointer<ImageViewer> guard(this);
    m_roiStatsRequest = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [frame, region, result](const TaskScheduler::TaskContext &ctx)
        {
            if (!ctx.isCancelled())
                *result = mviewer::core::computePreviewStatsROI(frame->pixels(), region);
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, frame, path, region, revision, result]()
        {
            if (!guard || !qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, frame, path, region, revision, result]()
                {
                    ImageViewer *viewer = guard.data();
                    if (!viewer || viewer->m_roiRevision != revision || viewer->m_frame != frame ||
                        viewer->m_currentPath != path || !result->valid)
                        return;
                    const QString text =
                        QString("ROI [%1,%2,%3,%4]: lum=%5, R=%6,G=%7,B=%8")
                            .arg(region.x)
                            .arg(region.y)
                            .arg(region.width)
                            .arg(region.height)
                            .arg(result->lumMean, 0, 'f', 1)
                            .arg(result->rMean)
                            .arg(result->gMean)
                            .arg(result->bMean);
                    emit viewer->regionStats(text);
                },
                Qt::QueuedConnection);
        });
}

void ImageViewer::advanceViewportRevision()
{
    ++m_viewRevision;
}

void ImageViewer::beginImageGeneration()
{
    ++m_imageGeneration;
    ++m_overlayGeneration;
    m_tileRequests.reset(m_imageGeneration);
    m_overlayRequests.reset(m_overlayGeneration);
    cancelRoiStats();
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
    // Decode off the UI thread: ImageRepository::loadAsyncCancellable dispatches
    // to the DecodePool, so first-open / next-prev never block the UI thread
    // (keeps within the performance budget: first <100ms, switch <20ms). The
    // decoded frame is applied back on the UI thread via QMetaObject::invokeMethod.
    //
    // M27 lifetime closure: the worker callback captures a QPointer and checks
    // it BEFORE any use — including the invokeMethod target, which is qApp
    // (always alive) rather than this. The queued lambda re-checks the guard,
    // the path AND the request generation (A -> B -> A: an older A request that
    // completes last must not overwrite the newer A).
    QPointer<ImageViewer> guard(this);
    auto onLoaded = [path, gen, guard](const ImageRepository::Result &res)
    {
        if (!guard)
            return; // viewer destroyed mid-decode
        if (!res.success())
        {
            QMetaObject::invokeMethod(
                qApp,
                [path, gen, guard]()
                {
                    ImageViewer *viewer = guard.data();
                    if (!viewer || path != viewer->m_currentPath || gen != viewer->m_requestGen)
                        return;
                    viewer->m_foregroundRequest.reset();
                    viewer->m_loading = false;
                    viewer->m_hasHistogram = false;
                    viewer->setWindowTitle(
                        QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
                    viewer->update();
                    emit viewer->loadFailed(path);
                });
            return;
        }
        QMetaObject::invokeMethod(
            qApp,
            [res, path, gen, guard]()
            {
                ImageViewer *viewer = guard.data();
                if (!viewer || path != viewer->m_currentPath || gen != viewer->m_requestGen)
                    return; // widget destroyed or user navigated away
                viewer->m_foregroundRequest.reset();
                viewer->m_loading = false;
                viewer->m_frame = res.frame;
                // M28 P1-02: no full-size QPixmap materialization on the UI
                // thread — the paint path renders tiles from the frame, and
                // the histogram comes from the frame's cached luminance pass.
                if (!viewer->m_frame || viewer->m_frame->pixels().isNull())
                {
                    viewer->m_hasHistogram = false;
                    viewer->setWindowTitle(
                        QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
                    viewer->update();
                    emit viewer->loadFailed(path);
                    return;
                }
                viewer->computeHistogram();
                const QFileInfo info(path);
                viewer->m_currentIndex = static_cast<int>(viewer->m_fileList.indexOf(path));
                // Build the render pipeline state (tile grid + fitted Viewport)
                // exactly as before — now applied on the UI thread post-decode.
                viewer->m_tiles =
                    TileGrid(viewer->m_frame->width(), viewer->m_frame->height(), 256);
                viewer->m_view.screenW = viewer->width();
                viewer->m_view.screenH = viewer->height();
                const FitPolicy fitPolicy =
                    viewer->isFullScreen() ||
                            viewer->property("mviewerFullscreenRequested").toBool()
                        ? FitPolicy::MaximizeClient
                        : FitPolicy::Comfortable;
                viewer->m_view.fit(viewer->m_frame->width(), viewer->m_frame->height(), fitPolicy);
                viewer->m_fitMode = true;
                const QString position = viewer->m_currentIndex >= 0
                                             ? QString(" [%1/%2]")
                                                   .arg(viewer->m_currentIndex + 1)
                                                   .arg(viewer->m_fileList.size())
                                             : QString();
                viewer->setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                                           .arg(info.fileName())
                                           .arg(viewer->m_frame->width())
                                           .arg(viewer->m_frame->height())
                                           .arg(position));
                // P1-7: if a session-restore zoom/pan was requested before the
                // async decode finished, apply it now. Only reuse the saved pan
                // offsets when the window size matches (offsets are screen-space);
                // otherwise keep the fitted pan and just restore the zoom level.
                if (viewer->m_pendingView)
                {
                    viewer->m_view.scale = viewer->m_pendingView->scale;
                    if (std::fabs(viewer->m_view.screenW - viewer->m_pendingView->screenW) < 2.0 &&
                        std::fabs(viewer->m_view.screenH - viewer->m_pendingView->screenH) < 2.0)
                    {
                        viewer->m_view.offsetX = viewer->m_pendingView->offsetX;
                        viewer->m_view.offsetY = viewer->m_pendingView->offsetY;
                    }
                    viewer->m_pendingView.reset();
                    viewer->m_fitMode = false; // restored zoom is explicit, not fit
                    emit viewer->zoomChanged(static_cast<int>(viewer->m_view.scale * 100.0 + 0.5));
                }
                viewer->m_overlayCache.clear();
                // Stage A: drop GPU textures for the previous image while
                // the GL context is current (UI thread only).
                if (viewer->context())
                {
                    viewer->makeCurrent();
                    viewer->m_gpu.clear();
                    viewer->doneCurrent();
                }
                viewer->preloadNeighbors(path);
                viewer->update();
                emit viewer->imageReady(viewer->m_frame);
                // Fullscreen layout can settle one event-loop turn after the
                // async decode delivery. Re-fit against the final geometry so
                // the first observable Browse frame is not based on the old
                // normal-window size.
                QTimer::singleShot(0, viewer,
                                   [guard, path, gen]()
                                   {
                                       ImageViewer *current = guard.data();
                                       if (!current || current->m_currentPath != path ||
                                           current->m_requestGen != gen)
                                           return;
                                       if (current->isFullScreen() && current->m_fitMode)
                                           current->fitToWidget();
                                       current->update();
                                   });
            });
    };
    // M29 preload promotion: if the previous navigation preloaded this exact
    // path, promote the matching neighbor preload to the foreground decode — a
    // queued match escalates to Decode priority, a running match reuses the
    // in-flight decode, and a finished/cancelled match resubmits at Decode
    // priority (already cache-warm). Other preloads were cancelled by
    // takeMatchingPreload above. A promotion that returns nullptr has already
    // delivered its explicit rejection error through the callback, so never
    // silently fall back to a second submission.
    if (matchingPreload)
        m_foregroundRequest =
            ImageRepository::instance().promotePreloadAsync(matchingPreload, std::move(onLoaded));
    else
        m_foregroundRequest = ImageRepository::instance().loadAsyncCancellable(path.toStdString(),
                                                                               std::move(onLoaded));
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
        ImageRepository::AsyncRequestHandle h =
            ImageRepository::instance().preloadAsync(m_fileList[i].toStdString());
        if (h)
            m_neighborPreloads.push_back({m_fileList[i], std::move(h)});
    }
}

void ImageViewer::cancelCurrentLoad()
{
    ImageRepository::instance().cancelAsync(m_foregroundRequest);
}

void ImageViewer::cancelPreloads()
{
    for (auto &p : m_neighborPreloads)
        ImageRepository::instance().cancelAsync(p.handle);
    m_neighborPreloads.clear();
}

ImageRepository::AsyncRequestHandle ImageViewer::takeMatchingPreload(const QString &path)
{
    // Consume the first tracked preload that exactly matches `path` and cancel
    // every other tracked preload, so a navigation back to a preloaded neighbor
    // can be promoted to the foreground decode instead of being re-queued. The
    // vector is emptied; at most one match is ever retained.
    ImageRepository::AsyncRequestHandle match;
    for (auto &p : m_neighborPreloads)
    {
        if (!match && p.path == path)
            match = std::move(p.handle);
        else
            ImageRepository::instance().cancelAsync(p.handle);
    }
    m_neighborPreloads.clear();
    return match;
}

void ImageViewer::emitZoom()
{
    emit zoomChanged(static_cast<int>(m_view.scale * 100.0 + 0.5));
}

void ImageViewer::fitToWidget()
{
    if (!m_frame || m_frame->pixels().isNull())
        return;
    // Delegate the fit math to Viewport; keep the Widget free of scale/offset.
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.fit(m_frame->width(), m_frame->height(),
               isFullScreen() || property("mviewerFullscreenRequested").toBool()
                   ? FitPolicy::MaximizeClient
                   : FitPolicy::Comfortable);
    advanceViewportRevision();
    m_fitMode = true;
    emitZoom();
}

void ImageViewer::zoomIn()
{
    if (!m_frame || m_frame->pixels().isNull())
        return;
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.zoomAt(width() / 2.0, height() / 2.0, kZoomStep);
    advanceViewportRevision();
    m_fitMode = false;
    emitZoom();
    update();
}

void ImageViewer::zoomOut()
{
    if (!m_frame || m_frame->pixels().isNull())
        return;
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.zoomAt(width() / 2.0, height() / 2.0, 1.0 / kZoomStep);
    advanceViewportRevision();
    m_fitMode = false;
    emitZoom();
    update();
}

void ImageViewer::zoomFit()
{
    fitToWidget(); // sets m_fitMode and emits zoomChanged
    update();
}

void ImageViewer::zoomActual()
{
    if (!m_frame || m_frame->pixels().isNull())
        return;
    // Keep the current view center stable while restoring 100%.
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.zoomAt(width() / 2.0, height() / 2.0, 1.0 / m_view.scale);
    advanceViewportRevision();
    m_fitMode = false;
    emitZoom();
    update();
}

QImage ImageViewer::currentImage() const
{
    if (!m_frame || m_frame->pixels().isNull())
        return QImage();
    // Copy/save means what the user sees. Materialize the display-only sRGB
    // contract (including embedded ICC) without modifying analysis pixels.
    return mvcore::toDisplayQImage(m_frame->pixels(), m_frame->metadata());
}

void ImageViewer::computeHistogram()
{
    std::fill(std::begin(m_histogram), std::end(m_histogram), 0);

    // Reuse the ImageFrame's cached luminance histogram (computed once on
    // decode inside ImageRepository). No re-decode in the QWidget layer.
    if (!m_frame || !m_frame->hasHistogram())
    {
        m_hasHistogram = false;
        return;
    }
    const auto &hist = m_frame->histogram();
    for (int i = 0; i < 256; ++i)
        m_histogram[i] = std::min(hist.luminance[i], 0x7FFFFFFF);
    m_hasHistogram = true;
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (!m_frame || m_frame->pixels().isNull())
        return;

    m_view.screenW = width();
    m_view.screenH = height();
    const QPointF mouse = event->position();
    const double factor = event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep;
    m_view.zoomAt(mouse.x(), mouse.y(), factor);
    advanceViewportRevision();
    m_fitMode = false;
    emitZoom();
    update();
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click toggles between fit-to-window and 100% at the cursor —
    // the standard image-viewer zoom gesture.
    if (event->button() != Qt::LeftButton || !m_frame || m_frame->pixels().isNull())
    {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    if (m_fitMode)
    {
        m_view.screenW = width();
        m_view.screenH = height();
        const QPointF p = event->position();
        m_view.zoomAt(p.x(), p.y(), 1.0 / m_view.scale);
        advanceViewportRevision();
        m_fitMode = false;
        emitZoom();
    }
    else
    {
        fitToWidget();
    }
    update();
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    // Mouse back/forward buttons (XButton1/2) navigate prev/next image.
    if (event->button() == Qt::BackButton)
    {
        emit requestPrev();
        return;
    }
    if (event->button() == Qt::ForwardButton)
    {
        emit requestNext();
        return;
    }
    if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) && m_frame &&
        m_frame->isValid())
    {
        if (m_selectMode && event->button() == Qt::LeftButton)
        {
            cancelRoiStats();
            m_selecting = true;
            m_selStart = m_selEnd = event->pos();
        }
        else
        {
            m_dragging = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    // Auto-hide cursor: any mouse movement restores the cursor and restarts
    // the hide timer (fullscreen only).
    if (m_cursorHidden)
    {
        m_cursorHidden = false;
        setCursor(m_selectMode ? Qt::CrossCursor : Qt::OpenHandCursor);
    }
    if (isFullScreen())
        m_cursorHideTimer->start();

    if (m_selecting)
    {
        m_selEnd = event->pos();
        update();
    }
    else if (m_dragging)
    {
        m_view.pan(event->pos().x() - m_lastMousePos.x(), event->pos().y() - m_lastMousePos.y());
        advanceViewportRevision();
        m_lastMousePos = event->pos();
        update();
    }

    // Pixel Inspector (P1 #6): read the pixel under the cursor from the shared
    // format-aware sampler (core/image/ImageBuffer.h), using the inverse of the
    // Viewport transform. samplePixel canonicalises RGB/RGBA/BGR/BGRA/grayscale
    // to RGBA and reports invalid (never an out-of-bounds read) for out-of-range
    // or truncated buffers.
    int ix = -1, iy = -1, r = 0, g = 0, b = 0, a = 255;
    bool valid = false;
    if (m_frame && m_frame->isValid())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        const double imgX = (event->pos().x() - m_view.offsetX) / m_view.scale;
        const double imgY = (event->pos().y() - m_view.offsetY) / m_view.scale;
        ix = static_cast<int>(std::floor(imgX));
        iy = static_cast<int>(std::floor(imgY));
        const PixelRGBA px = samplePixel(m_frame->pixels(), ix, iy);
        r = px.r;
        g = px.g;
        b = px.b;
        a = px.a;
        valid = px.valid;
    }
    // P0-2/PixelInspector: also surface the original high-bit-depth sample when
    // available. rawKind: 0 = 8-bit only, 1 = RAW preview (demosaic 8-bit),
    // 2 = true 16-bit integer samples present.
    int r16 = 0, g16 = 0, b16 = 0, rawKind = 0;
    if (m_frame)
    {
        const auto &meta = m_frame->metadata();
        if (meta.format == "RAW")
        {
            rawKind = 1;
        }
        else if (m_frame->hasRaw16() && valid)
        {
            uint16_t vr = 0, vg = 0, vb = 0;
            if (m_frame->raw16At(ix, iy, vr, vg, vb))
            {
                r16 = vr;
                g16 = vg;
                b16 = vb;
                rawKind = 2;
            }
        }
    }
    emit pixelInfo(ix, iy, r, g, b, a, r16, g16, b16, rawKind, valid);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
    {
        if (m_selecting && event->button() == Qt::LeftButton)
        {
            m_selecting = false;
            const QRect r = selectedRegion();
            if (r.width() > 5 && r.height() > 5)
            {
                // M26: compute ROI statistics directly from the decoded
                // ImageData (worker-side helper) — no full-image
                // QPixmap->QImage conversion on the UI thread.
                const QRect imgRect =
                    QRect(static_cast<int>(std::floor((r.x() - m_view.offsetX) / m_view.scale)),
                          static_cast<int>(std::floor((r.y() - m_view.offsetY) / m_view.scale)),
                          static_cast<int>(std::round(r.width() / m_view.scale)),
                          static_cast<int>(std::round(r.height() / m_view.scale)))
                        .normalized();
                const QRect valid =
                    m_frame ? imgRect.intersected(QRect(0, 0, m_frame->width(), m_frame->height()))
                            : QRect();
                if (!valid.isEmpty() && m_frame)
                {
#if 0
                    const mviewer::domain::Selection sel{valid.x(), valid.y(), valid.width(),
                                                         valid.height()};
                    const auto stats =
                        mviewer::core::computePreviewStats(cropRegion(m_frame->pixels(), sel));
                    const QString text = QString("框选 [%1,%2,%3,%4]: 亮度=%5, R=%6,G=%7,B=%8")
                                             .arg(valid.x())
                                             .arg(valid.y())
                                             .arg(valid.width())
                                             .arg(valid.height())
                                             .arg(stats.lumMean, 0, 'f', 1)
                                             .arg(static_cast<double>(stats.rMean), 0, 'f', 1)
                                             .arg(static_cast<double>(stats.gMean), 0, 'f', 1)
                                             .arg(static_cast<double>(stats.bMean), 0, 'f', 1);
                    emit regionStats(text);
                    emit selectionChanged(valid); // new: live ROI stats
                }
 #endif
                    scheduleRoiStats(valid);
                    emit selectionChanged(valid);
                }
                else
                {
                    cancelRoiStats();
                    emit selectionChanged(QRect());
                }
            }
        }
        else
        {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
    }
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Keep the image fitted across window resizes while in fit mode; an
    // explicit zoom (wheel/keyboard/double-click) opts out of re-fitting.
    if (m_fitMode && m_frame && m_frame->isValid())
        fitToWidget();
    else if (m_fitMode && !m_provisionalImage.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        const QSize source = m_provisionalSourceSize.isValid() ? m_provisionalSourceSize
                                                                : m_provisionalImage.size();
        const FitPolicy fitPolicy =
            isFullScreen() || property("mviewerFullscreenRequested").toBool()
                ? FitPolicy::MaximizeClient
                : FitPolicy::Comfortable;
        m_view.fit(source.width(), source.height(), fitPolicy);
        advanceViewportRevision();
        emitZoom();
    }
}

QRect ImageViewer::selectedRegion() const
{
    return QRect(m_selStart, m_selEnd).normalized();
}

void ImageViewer::setSelectMode(bool on)
{
    m_selectMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    update();
}

void ImageViewer::setOverlayMode(mviewer::OverlayMode m)
{
    if (m_overlayMode == m)
        return;
    m_overlayMode = m;
    ++m_overlayGeneration;
    m_overlayRequests.reset(m_overlayGeneration);
    m_overlayCache.clear();
    QSettings s;
    s.setValue("defaultAnalysisOverlay", static_cast<int>(m));
    update();
}

void ImageViewer::setZebraThreshold(int t)
{
    t = qBound(1, t, 40);
    if (m_zebraThreshold == t)
        return;
    m_zebraThreshold = t;
    ++m_overlayGeneration;
    m_overlayRequests.reset(m_overlayGeneration);
    m_overlayCache.clear();
    // Only need a repaint when an overlay is currently visible.
    if (m_overlayMode != mviewer::OverlayMode::None)
        update();
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    if (key == Qt::Key_Left)
        emit requestPrev();
    else if (key == Qt::Key_Right)
        emit requestNext();
    // Home/End/PageUp/PageDown are handled via eventFilter → MainWindow's
    // keyPressEvent, so they work identically in the viewer and the gallery.
    else if (key == Qt::Key_Plus || key == Qt::Key_Equal)
        zoomIn();
    else if (key == Qt::Key_Minus || key == Qt::Key_Underscore)
        zoomOut();
    else if (key == Qt::Key_0 && !(mods & Qt::ControlModifier))
        zoomFit();
    else if (key == Qt::Key_1 && !(mods & Qt::ControlModifier))
        zoomActual();
    // Also accept Ctrl+0 / Ctrl+1 as zoom shortcuts (widely expected by users).
    else if (key == Qt::Key_0 && (mods & Qt::ControlModifier))
        zoomFit();
    else if (key == Qt::Key_1 && (mods & Qt::ControlModifier))
        zoomActual();
    // R toggles region-of-interest selection mode.
    else if (key == Qt::Key_R && !mods)
        setSelectMode(!m_selectMode);
    else if ((key == Qt::Key_F && !mods) || key == Qt::Key_F11)
    {
        const bool fullscreen = isFullScreen() || property("mviewerFullscreenRequested").toBool();
        if (fullscreen)
        {
            setProperty("mviewerFullscreenRequested", false);
            showNormal();
            QPointer<ImageViewer> guard(this);
            QTimer::singleShot(0, this,
                               [guard]()
                               {
                                   if (!guard)
                                       return;
                                   if (guard->m_fitMode && guard->m_frame)
                                       guard->fitToWidget();
                                   guard->update();
                               });
        }
        else
        {
            setProperty("mviewerFullscreenRequested", true);
            showFullScreen();
        }
    }
    else if (key == Qt::Key_Escape)
        close();
    else
        QWidget::keyPressEvent(event);
}
