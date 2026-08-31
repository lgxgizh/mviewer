#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "application/ImageLoadingService.h"
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
    m_lifetime = mviewer::core::AsyncLifetimeToken::create();
    setWindowTitle("图片查看");
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    // The viewer is a standalone top-level window. Explicitly opt into
    // keyboard focus so ESC and the viewer's navigation/zoom keys are not left
    // with the thumbnail list after a double-click open.
    setFocusPolicy(Qt::StrongFocus);
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
                if (property("mviewerFullscreenRequested").toBool() && !m_dragging &&
                    !m_selecting)
                {
                    m_cursorHidden = true;
                    setCursor(Qt::BlankCursor);
                }
            });
}

ImageViewer::~ImageViewer()
{
    // M46: invalidate the consumer-lifetime token FIRST. Every request this
    // viewer owns holds only a weak_ptr to it; once invalidated, the
    // repository suppresses any late client delivery before it can start. The
    // cancels below then also wait (bounded) for a delivery that already
    // started, so after this destructor returns no callback is running or will
    // run against this viewer.
    m_lifetime->invalidate();
    // M29: drop any in-flight foreground decode / neighbor preload before
    // tearing down the GL context. A worker callback that lands after teardown
    // is harmless (the QPointer/path/generation guards suppress delivery), but
    // cancelling first also removes the wasted full-resolution decode work.
    cancelCurrentLoad();
    cancelPreloads();
    cancelDisplayRasterPreloads();
    cancelRoiStats();
    cancelExportJob();
    // M47: drop any in-flight LOD/region display raster request (queued work
    // never starts; an already-running decode finishes bounded and its
    // delivery is suppressed by the QPointer/lifetime guards).
    cancelDisplayRequest();
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




void ImageViewer::toggleFullscreen()
{
    setFullscreenRequested(!property("mviewerFullscreenRequested").toBool());
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
        property("mviewerFullscreenRequested").toBool() ? FitPolicy::MaximizeClient
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
    cancelDisplayRasterPreloads();
    cancelDisplayRequest();
    cancelExportJob();
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
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
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
                    ImageViewer *viewer = guard->data();
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
    // M47: any pan/zoom/fit/resize may invalidate the current display raster's
    // coverage or density; the (debounced) upgrade check runs on the next
    // event-loop turn.
    if (m_lodMode)
        scheduleDisplayUpgrade();
}

void ImageViewer::beginImageGeneration()
{
    ++m_imageGeneration;
    ++m_overlayGeneration;
    m_tileRequests.reset(m_imageGeneration);
    m_overlayRequests.reset(m_overlayGeneration);
    cancelRoiStats();
}






void ImageViewer::cancelExportJob()
{
    ++m_exportGeneration;
    if (m_exportCancel)
        m_exportCancel->store(true, std::memory_order_release);
    TaskScheduler::cancel(m_exportTask);
    m_exportTask.reset();
    m_exportCancel.reset();
}

void ImageViewer::startExportJob(mviewer::exportjob::ExportJobConfig cfg, bool clipboard,
                                 const QString &destination)
{
    cancelExportJob();
    if (cfg.sources.empty())
        return;

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    cfg.cancel = cancel;
    auto result = std::make_shared<mviewer::exportjob::ExportJobResult>();
    const uint64_t generation = m_exportGeneration;
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    m_exportCancel = cancel;
    m_exportTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [cfg, result](const TaskScheduler::TaskContext &ctx)
        {
            if (!ctx.isCancelled())
                *result = mviewer::exportjob::run(cfg);
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, result, cancel, generation, clipboard, destination]()
        {
            if (cancel->load(std::memory_order_acquire) || !qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result, cancel, generation, clipboard, destination]()
                {
                    ImageViewer *viewer = guard ? guard->data() : nullptr;
                    if (!viewer || generation != viewer->m_exportGeneration ||
                        cancel->load(std::memory_order_acquire))
                        return;
                    viewer->m_exportTask.reset();
                    viewer->m_exportCancel.reset();
                    if (clipboard && result->done > 0 && !result->clipboardImage.isNull())
                    {
                        QImage image = mvcore::toQImageRef(result->clipboardImage);
                        if (image.isNull())
                            image = mvcore::toQImage(result->clipboardImage);
                        if (!image.isNull())
                            QApplication::clipboard()->setImage(image);
                    }
                    emit viewer->exportFinished(result->failed == 0 && result->done > 0,
                                                QString::fromStdString(result->message));
                    Q_UNUSED(destination);
                },
                Qt::QueuedConnection);
        });
}

void ImageViewer::copyToClipboard(const QString &path)
{
    const QString source = path.isEmpty() ? m_currentPath : path;
    if (source.isEmpty())
        return;
    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mviewer::exportjob::Mode::Clipboard;
    cfg.sources.push_back(source.toStdString());
    cfg.frameIndex = (source == m_currentPath) ? frameIndex() : 0;
    cfg.preserveDisplayAppearance = true;
    startExportJob(std::move(cfg), true);
}

void ImageViewer::saveToPath(const QString &path)
{
    if (path.isEmpty() || m_currentPath.isEmpty())
        return;
    const QFileInfo info(path);
    QString format = info.suffix().toLower();
    if (format == "jpg")
        format = "jpeg";
    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mviewer::exportjob::Mode::Convert;
    cfg.sources.push_back(m_currentPath.toStdString());
    cfg.frameIndex = frameIndex();
    cfg.outDir = info.absolutePath().toStdString();
    cfg.destinationPath = path.toStdString();
    cfg.format = format.toStdString();
    cfg.preserveDisplayAppearance = true;
    startExportJob(std::move(cfg), false, path);
}


void ImageViewer::emitZoom()
{
    emit zoomChanged(static_cast<int>(m_view.scale * 100.0 + 0.5));
}

bool ImageViewer::hasDisplayImage() const
{
    return (m_frame && m_frame->isValid()) || (m_lodMode && !m_raster.image.isNull());
}

QSize ImageViewer::displaySize() const
{
    if (m_frame && m_frame->isValid())
        return QSize(m_frame->width(), m_frame->height());
    if (m_lodMode && !m_raster.image.isNull())
        return m_raster.sourceSize;
    return QSize();
}

void ImageViewer::fitToWidget()
{
    if (!hasDisplayImage())
        return;
    // Delegate the fit math to Viewport; keep the Widget free of scale/offset.
    m_view.screenW = width();
    m_view.screenH = height();
    const QSize size = displaySize();
    m_view.fit(size.width(), size.height(),
               property("mviewerFullscreenRequested").toBool() ? FitPolicy::MaximizeClient
                                                                 : FitPolicy::Comfortable);
    advanceViewportRevision();
    m_fitMode = true;
    emitZoom();
}

void ImageViewer::zoomIn()
{
    if (!hasDisplayImage())
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
    if (!hasDisplayImage())
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
    if (!hasDisplayImage())
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
    if (!hasDisplayImage())
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
    if (event->button() != Qt::LeftButton || !hasDisplayImage())
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
    if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) &&
        hasDisplayImage())
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
    if (property("mviewerFullscreenRequested").toBool())
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
        property("mviewerFullscreenRequested").toBool() ? FitPolicy::MaximizeClient
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
    if (handleFrameKey(key, mods) || handleNavigationKey(key) || handleZoomKey(key, mods) ||
        handleModeKey(key, mods))
        return;
    QWidget::keyPressEvent(event);
}

bool ImageViewer::handleNavigationKey(int key)
{
    if (key == Qt::Key_Left)
        emit requestPrev();
    else if (key == Qt::Key_Right)
        emit requestNext();
    else
        return false;
    return true;
}

bool ImageViewer::handleZoomKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (key == Qt::Key_Plus || key == Qt::Key_Equal)
        zoomIn();
    else if (key == Qt::Key_Minus || key == Qt::Key_Underscore)
        zoomOut();
    else if (key == Qt::Key_0)
        zoomFit();
    else if (key == Qt::Key_1)
        zoomActual();
    else
        return false;
    Q_UNUSED(modifiers); // Ctrl+0/Ctrl+1 intentionally share the same zoom action.
    return true;
}

bool ImageViewer::handleModeKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (key == Qt::Key_R && !modifiers)
        setSelectMode(!m_selectMode);
    else if ((key == Qt::Key_F && !modifiers) || key == Qt::Key_F11)
        toggleFullscreen();
    else if (key == Qt::Key_Escape)
        close();
    else
        return false;
    return true;
}
