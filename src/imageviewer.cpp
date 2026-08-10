#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFormats.h"
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
#include <QDir>
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

namespace
{
// M25: the shipped-format SSOT drives the viewer's own directory navigation
// (RAW/WebP/GIF included — the viewer must see the same images as the gallery).
// LAZY on purpose: building it at static-init time would query the Qt image
// format plugins before QCoreApplication exists and cache an incomplete set.
QStringList imageExtensionFilters()
{
    QStringList filters;
    for (const auto &w : mviewer::core::ImageFormats::wildcardFilters())
        filters << QString::fromStdString(w);
    return filters;
}
const double kZoomStep = 1.15;
} // namespace

QStringList ImageViewer::listImages(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries = dir.entryInfoList(imageExtensionFilters(), QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(entries.size());
    for (const QFileInfo &info : entries)
        result.append(info.absoluteFilePath());
    return result;
}

ImageViewer::ImageViewer(QWidget *parent) : QOpenGLWidget(parent)
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
    QSettings settings;
    settings.setValue("viewerGeometry", saveGeometry());
    event->accept();
}

void ImageViewer::setImage(const QString &path)
{
    m_currentPath = path;
    // Decode off the UI thread: ImageRepository::loadAsync dispatches to the
    // DecodePool, so first-open / next-prev never block the UI thread (keeps
    // within the performance budget: first <100ms, switch <20ms). The decoded
    // frame is applied back on the UI thread via QMetaObject::invokeMethod.
    //
    // M27 lifetime closure: the worker callback captures a QPointer and checks
    // it BEFORE any use — including the invokeMethod target, which is qApp
    // (always alive) rather than this. The queued lambda re-checks the guard,
    // the path AND the request generation (A -> B -> A: an older A request that
    // completes last must not overwrite the newer A).
    const uint64_t gen = ++m_requestGen;
    QPointer<ImageViewer> guard(this);
    ImageRepository::instance().loadAsync(
        path.toStdString(),
        [path, gen, guard](const ImageRepository::Result &res)
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
                    viewer->m_fileList = listImages(info.absolutePath());
                    viewer->m_currentIndex = static_cast<int>(viewer->m_fileList.indexOf(path));
                    // Build the render pipeline state (tile grid + fitted Viewport)
                    // exactly as before — now applied on the UI thread post-decode.
                    viewer->m_tiles =
                        TileGrid(viewer->m_frame->width(), viewer->m_frame->height(), 256);
                    viewer->m_view.screenW = viewer->width();
                    viewer->m_view.screenH = viewer->height();
                    viewer->m_view.fit(viewer->m_frame->width(), viewer->m_frame->height(), 0.95);
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
                        if (std::fabs(viewer->m_view.screenW - viewer->m_pendingView->screenW) <
                                2.0 &&
                            std::fabs(viewer->m_view.screenH - viewer->m_pendingView->screenH) <
                                2.0)
                        {
                            viewer->m_view.offsetX = viewer->m_pendingView->offsetX;
                            viewer->m_view.offsetY = viewer->m_pendingView->offsetY;
                        }
                        viewer->m_pendingView.reset();
                        viewer->m_fitMode = false; // restored zoom is explicit, not fit
                        emit viewer->zoomChanged(
                            static_cast<int>(viewer->m_view.scale * 100.0 + 0.5));
                    }
                    viewer->m_tileCache.clear(); // drop tiles from any previously viewed image
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
                });
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

    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size())
            continue;
        if (m_fileList[i] == path)
            continue;
        // Warm the cache only (off UI thread). Do NOT call loadPixmap(): it
        // assigns m_frame, which would race with the UI thread's frame() read.
        ImageRepository::instance().loadAsync(m_fileList[i].toStdString(),
                                              [](const ImageRepository::Result &) {});
    }
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
    m_view.fit(m_frame->width(), m_frame->height(), 0.95);
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
    m_fitMode = false;
    emitZoom();
    update();
}

QImage ImageViewer::currentImage() const
{
    if (!m_frame || m_frame->pixels().isNull())
        return QImage();
    // Zero-copy alias when the byte order matches; the caller (copy/save) owns
    // the result, so this is safe.
    QImage img = mvcore::toQImageRef(m_frame->pixels());
    if (img.isNull())
        img = mvcore::toQImage(m_frame->pixels()); // RGBA32 fallback
    return img;
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
        m_lastMousePos = event->pos();
        update();
    }

    // Pixel Inspector (P1 #6): read the pixel under the cursor directly from
    // the ImageFrame (RGB24/RGBA32), using the inverse of the Viewport transform.
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
        const int iw = m_frame->width();
        const int ih = m_frame->height();
        if (ix >= 0 && ix < iw && iy >= 0 && iy < ih)
        {
            const ImageBuffer view = m_frame->pixels().view();
            if (view.channelsPerPixel() >= 3)
            {
                const uint8_t *p = view.data + static_cast<size_t>(iy) * view.stride() +
                                   static_cast<size_t>(ix) * view.channelsPerPixel();
                r = p[0];
                g = p[1];
                b = p[2];
                if (view.channelsPerPixel() >= 4)
                    a = p[3];
                valid = true;
            }
        }
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
                else
                {
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
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    }
    else if (key == Qt::Key_Escape)
    {
        if (isFullScreen())
            showNormal();
        else
            close();
    }
    else
        QWidget::keyPressEvent(event);
}
