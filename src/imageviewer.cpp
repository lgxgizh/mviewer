#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageRepository.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRect>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>

namespace
{
const QStringList kImageExtensions = {"*.jpg", "*.jpeg", "*.bmp",  "*.png",
                                      "*.tif", "*.tiff", "*.webp", "*.gif"};
const double kZoomStep = 1.15;
} // namespace

QStringList ImageViewer::listImages(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries = dir.entryInfoList(kImageExtensions, QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(entries.size());
    for (const QFileInfo &info : entries)
        result.append(info.absoluteFilePath());
    return result;
}

ImageViewer::ImageViewer(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("图片查看");
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(200, 200);
    // Restore window geometry from the last session.
    QSettings settings;
    const QByteArray geom = settings.value("viewerGeometry").toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    else
        resize(900, 700);
    // Auto-hide cursor in fullscreen after 2.5s of inactivity.
    m_cursorHideTimer = new QTimer(this);
    m_cursorHideTimer->setSingleShot(true);
    m_cursorHideTimer->setInterval(2500);
    connect(m_cursorHideTimer, &QTimer::timeout, this, [this]()
    {
        if (isFullScreen() && !m_dragging && !m_selecting)
        {
            m_cursorHidden = true;
            setCursor(Qt::BlankCursor);
        }
    });
}

ImageViewer::~ImageViewer() = default;

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
    QPointer<ImageViewer> guard(this);
    ImageRepository::instance().loadAsync(
        path.toStdString(),
        [this, path, guard](const ImageRepository::Result &res)
        {
            if (!res.success())
            {
                QMetaObject::invokeMethod(
                    this,
                    [this, path, guard]()
                    {
                        if (!guard || path != m_currentPath)
                            return;
                        m_hasHistogram = false;
                        setWindowTitle(
                            QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
                        update();
                        emit loadFailed(path);
                    });
                return;
            }
            QMetaObject::invokeMethod(
                this,
                [this, res, path, guard]()
                {
                    if (!guard || path != m_currentPath)
                        return; // widget destroyed or user navigated away
                    m_frame = res.frame;
                    m_pixmap = QPixmap::fromImage(mvcore::toQImage(m_frame->pixels()));
                    if (m_pixmap.isNull())
                    {
                        m_hasHistogram = false;
                        setWindowTitle(
                            QString("无法加载 - %1 - MViewer").arg(QFileInfo(path).fileName()));
                        update();
                        emit loadFailed(path);
                        return;
                    }
                    computeHistogram(m_pixmap);
                    const QFileInfo info(path);
                    m_fileList = listImages(info.absolutePath());
                    m_currentIndex = static_cast<int>(m_fileList.indexOf(path));
                    // Build the render pipeline state (tile grid + fitted Viewport)
                    // exactly as before — now applied on the UI thread post-decode.
                    m_tiles = TileGrid(m_pixmap.width(), m_pixmap.height(), 256);
                    m_view.screenW = width();
                    m_view.screenH = height();
                    m_view.fit(m_pixmap.width(), m_pixmap.height(), 0.95);
                    m_fitMode = true;
                    const QString position =
                        m_currentIndex >= 0
                            ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                            : QString();
                    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                                       .arg(info.fileName())
                                       .arg(m_pixmap.width())
                                       .arg(m_pixmap.height())
                                       .arg(position));
                    // P1-7: if a session-restore zoom/pan was requested before the
                    // async decode finished, apply it now. Only reuse the saved pan
                    // offsets when the window size matches (offsets are screen-space);
                    // otherwise keep the fitted pan and just restore the zoom level.
                    if (m_pendingView)
                    {
                        m_view.scale = m_pendingView->scale;
                        if (std::fabs(m_view.screenW - m_pendingView->screenW) < 2.0 &&
                            std::fabs(m_view.screenH - m_pendingView->screenH) < 2.0)
                        {
                            m_view.offsetX = m_pendingView->offsetX;
                            m_view.offsetY = m_pendingView->offsetY;
                        }
                        m_pendingView.reset();
                        m_fitMode = false; // restored zoom is explicit, not fit
                        emitZoom();
                    }
                    m_tileCache.clear(); // drop tiles from any previously viewed image
                    preloadNeighbors(path);
                    update();
                    emit imageReady(m_frame);
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
    if (m_pixmap.isNull())
        return;
    // Delegate the fit math to Viewport; keep the Widget free of scale/offset.
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.fit(m_pixmap.width(), m_pixmap.height(), 0.95);
    m_fitMode = true;
    emitZoom();
}

void ImageViewer::zoomIn()
{
    if (m_pixmap.isNull())
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
    if (m_pixmap.isNull())
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
    if (m_pixmap.isNull())
        return;
    // Keep the current view center stable while restoring 100%.
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.zoomAt(width() / 2.0, height() / 2.0, 1.0 / m_view.scale);
    m_fitMode = false;
    emitZoom();
    update();
}

void ImageViewer::computeHistogram(const QPixmap &pixmap)
{
    Q_UNUSED(pixmap);
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

void ImageViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    // Render Pipeline (P1-①): the Widget is the Viewport owner. It asks the
    // TileGrid which source tiles are visible, then asks the Renderer to scale
    // each visible region from the ImageFrame pixels — never decoding, never
    // rasterizing the whole image into one bitmap. This is what makes 100MP/
    // RAW rendering feasible later.
    if (!m_pixmap.isNull() && m_frame)
    {
        MV_TRACE_SCOPED("ImageViewer::paint");
        // Keep Viewport in *logical* widget pixels so mouse pan/zoom math stays
        // consistent. HiDPI sharpness is handled by decoding tiles at device
        // resolution (see dpr scale below) without changing interaction space.
        m_view.screenW = width();
        m_view.screenH = height();
        const qreal dpr = devicePixelRatioF();
        if (dpr > 1.0)
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        RenderEngine &eng = RenderEngine::instance();
        // Render Pipeline (P1-①): ask the TileCache for the visible tiles at
        // the LOD chosen for the current zoom. Only missing tiles are decoded
        // (via RenderEngine::scaleRegion in core/), then cached. The Widget
        // never decodes and never rasterizes the whole image.
        const std::string id = m_frame->id().hash;
        // A-8.3: on HiDPI, request tiles at device-pixel size so the blit is
        // sharp when Qt scales the widget. Interaction coords stay logical.
        Viewport tileView = m_view;
        if (dpr > 1.0)
        {
            tileView.screenW = static_cast<int>(std::lround(m_view.screenW * dpr));
            tileView.screenH = static_cast<int>(std::lround(m_view.screenH * dpr));
            tileView.scale = m_view.scale * dpr;
            tileView.offsetX = m_view.offsetX * dpr;
            tileView.offsetY = m_view.offsetY * dpr;
        }
        auto ready = m_tileCache.request(
            id, tileView, m_tiles,
            [&](const std::string &, int sx, int sy, int sw, int sh, int tw, int th) -> ImageData
            {
                const RenderRect region{sx, sy, sw, sh};
                const RenderSize tgt{tw, th};
                return eng.scaleRegion(m_frame->pixels(), region, tgt,
                                       m_view.scale < 1.0 ? RenderInterp::Bilinear
                                                          : RenderInterp::Nearest);
            });
        for (const auto &rt : ready)
        {
            // M16: when the GPU tier is enabled (real GL context +
            // MVIEWER_GPU=1), ensure the decoded tile is uploaded to a
            // resident texture exactly once; re-paints composite from the
            // handle instead of re-converting to QImage on the CPU. The
            // upload is a no-op when disabled (CPU path stays the default
            // and is the verified-green fallback everywhere, including here).
            if (GpuTileUploader::enabled())
            {
                const ImageBuffer view = rt.data.view();
                m_gpu.ensure(rt.key, view.data, view.width, view.height, view.channelsPerPixel());
            }
            // Compute on-screen rect for this tile's LOD/source region.
            // When tiles were decoded in device space, convert back to logical
            // widget coords for QPainter (which works in logical pixels).
            int tsx, tsy, tsw, tsh;
            tileView.imageRectToScreen(rt.key.col * m_tiles.tileSize * (1 << rt.key.lod),
                                       rt.key.row * m_tiles.tileSize * (1 << rt.key.lod),
                                       TileCache::lodTileSize(m_tiles.tileSize, rt.key.lod),
                                       TileCache::lodTileSize(m_tiles.tileSize, rt.key.lod), tsx,
                                       tsy, tsw, tsh);
            if (dpr > 1.0)
            {
                tsx = static_cast<int>(std::lround(tsx / dpr));
                tsy = static_cast<int>(std::lround(tsy / dpr));
                tsw = static_cast<int>(std::lround(tsw / dpr));
                tsh = static_cast<int>(std::lround(tsh / dpr));
            }
            QImage q = mvcore::toQImage(rt.data);
            if (q.isNull())
                continue;
            // Tile pixels are at device resolution; target rect is logical.
            // QPainter scales the denser source into the logical rect → sharp
            // on HiDPI without setDevicePixelRatio (which would double-scale).
            painter.drawImage(QRect(tsx, tsy, tsw, tsh), q);
        }
    }
    else if (!m_currentPath.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "无法加载图片");
    }
    else
    {
        // Empty state: no image loaded yet — prompt the user to open one.
        painter.setPen(QColor(180, 180, 180));
        QFont f = painter.font();
        f.setPointSize(f.pointSize() + 2);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter,
                         "拖放图片或文件夹到此处\n"
                         "或按 Ctrl+O 打开目录\n"
                         "或双击缩略图查看");
    }

    if (m_hasHistogram)
        drawHistogram(painter);

    if (m_selectMode)
    {
        const QRect r = m_selecting ? QRect(m_selStart, m_selEnd).normalized() : selectedRegion();
        if (r.isValid() && (m_selecting || r.width() > 5) && (m_selecting || r.height() > 5))
        {
            painter.save();
            painter.setPen(QPen(QColor(255, 255, 0), 1));
            painter.setBrush(QColor(255, 255, 0, 80));
            painter.drawRect(r);
            // Show live dimensions while dragging.
            if (m_selecting && m_pixmap.width() > 0)
            {
                const int imgW = static_cast<int>(r.width() / m_view.scale);
                const int imgH = static_cast<int>(r.height() / m_view.scale);
                const QString sizeText = QString("%1×%2").arg(imgW).arg(imgH);
                painter.setPen(QColor(255, 255, 0));
                QFont f = painter.font();
                f.setBold(true);
                painter.setFont(f);
                painter.drawText(r.bottomRight() + QPoint(8, 14), sizeText);
            }
            painter.restore();
        }
    }
}

void ImageViewer::drawHistogram(QPainter &painter) const
{
    const int w = 160;
    const int h = 90;
    const int margin = 10;
    const QRect bg(margin, margin, w, h);

    painter.save();
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bg.adjusted(-4, -4, 4, 4), 6, 6);

    int maxVal = 1;
    for (int i = 0; i < 256; ++i)
        maxVal = std::max(maxVal, m_histogram[i]);

    painter.setPen(QColor(255, 255, 255, 200));
    painter.setBrush(Qt::NoBrush);
    const double dx = static_cast<double>(w) / 256.0;
    const double dy = static_cast<double>(h) / maxVal;

    QPointF prev;
    for (int i = 0; i < 256; ++i)
    {
        const double x = bg.left() + i * dx;
        const double y = bg.bottom() - m_histogram[i] * dy;
        const QPointF cur(x, y);
        if (i > 0)
            painter.drawLine(prev, cur);
        prev = cur;
    }
    painter.restore();
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (m_pixmap.isNull())
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
    if (event->button() != Qt::LeftButton || m_pixmap.isNull())
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
    if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) &&
        !m_pixmap.isNull())
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
    if (m_frame && !m_pixmap.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        const double imgX = (event->pos().x() - m_view.offsetX) / m_view.scale;
        const double imgY = (event->pos().y() - m_view.offsetY) / m_view.scale;
        ix = static_cast<int>(std::floor(imgX));
        iy = static_cast<int>(std::floor(imgY));
        const int iw = m_pixmap.width();
        const int ih = m_pixmap.height();
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
    emit pixelInfo(ix, iy, r, g, b, a, valid);
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
                QImage img = m_pixmap.toImage().convertToFormat(QImage::Format_RGB32);
                const QRect imgRect =
                    QRect(static_cast<int>(std::floor((r.x() - m_view.offsetX) / m_view.scale)),
                          static_cast<int>(std::floor((r.y() - m_view.offsetY) / m_view.scale)),
                          static_cast<int>(std::round(r.width() / m_view.scale)),
                          static_cast<int>(std::round(r.height() / m_view.scale)))
                        .normalized();
                const QRect valid = imgRect.intersected(QRect(0, 0, img.width(), img.height()));
                if (!valid.isEmpty())
                {
                    const ImageStats stats =
                        AnalysisEngine::computeStats(mvcore::fromQImage(img.copy(valid)));
                    const QString text = QString("框选 [%1,%2,%3,%4]: 亮度=%5, R=%6,G=%7,B=%8")
                                             .arg(valid.x())
                                             .arg(valid.y())
                                             .arg(valid.width())
                                             .arg(valid.height())
                                             .arg(stats.lumMean, 0, 'f', 1)
                                             .arg(stats.rMean, 0, 'f', 1)
                                             .arg(stats.gMean, 0, 'f', 1)
                                             .arg(stats.bMean, 0, 'f', 1);
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
    if (m_fitMode && !m_pixmap.isNull())
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

void ImageViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *aCopy = menu.addAction("复制图片");
    QAction *aCopyPath = menu.addAction("复制路径");
    QAction *aCopyColor = menu.addAction("复制像素颜色 (#RRGGBB)");
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    menu.addSeparator();
    QAction *aZoomIn = menu.addAction("放大 (+)");
    QAction *aZoomOut = menu.addAction("缩小 (-)");
    QAction *aZoomFit = menu.addAction("适应窗口 (0)");
    QAction *aZoomActual = menu.addAction("实际大小 (1)");
    menu.addSeparator();
    QAction *aSelectRegion = menu.addAction("框选区域 (R)");
    aSelectRegion->setCheckable(true);
    aSelectRegion->setChecked(m_selectMode);
    menu.addSeparator();
    // A-7.3: "分析" submenu — list every registered analyzer for one-click run.
    QMenu *analyzeMenu = menu.addMenu("分析");
    QList<QAction *> analyzeActions;
    if (m_frame)
    {
        const auto ids = AnalyzerRegistry::instance().availableAnalyzers();
        for (const auto &id : ids)
        {
            const auto info = AnalyzerRegistry::instance().infoFor(id);
            const QString label =
                info ? QString::fromStdString(info->name) : QString::fromStdString(id);
            QAction *a = analyzeMenu->addAction(label);
            a->setData(QString::fromStdString(id));
            analyzeActions.append(a);
        }
        if (analyzeActions.isEmpty())
            analyzeMenu->addAction("（无可用分析器）")->setEnabled(false);
    }
    else
    {
        analyzeMenu->addAction("（请先打开图片）")->setEnabled(false);
    }
    menu.addSeparator();
    QAction *aNext = menu.addAction("下一张 (→)");
    QAction *aPrev = menu.addAction("上一张 (←)");
    menu.addSeparator();
    QAction *aFullscreen = menu.addAction("全屏 (F)");
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    // A-7.3: route analyzer selection through AnalysisPanel (unified entry).
    // MainWindow shows the panel and runs the analyzer so results land in the
    // Plugin tab — not a one-off QMessageBox.
    if (analyzeActions.contains(chosen) && m_frame)
    {
        emit analysisRequested(chosen->data().toString());
        return;
    }
    if (chosen == aCopy)
        QApplication::clipboard()->setPixmap(m_pixmap);
    else if (chosen == aCopyPath)
        QApplication::clipboard()->setText(m_currentPath);
    else if (chosen == aCopyColor)
    {
        // Read pixel color at the cursor position (event->pos() in widget coords).
        const QPoint pos = event->pos();
        if (!m_pixmap.isNull() && m_frame)
        {
            const int iw = m_pixmap.width();
            const int ih = m_pixmap.height();
            const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
            const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
            if (ix >= 0 && ix < iw && iy >= 0 && iy < ih)
            {
                const ImageBuffer view = m_frame->pixels().view();
                if (view.channelsPerPixel() >= 3)
                {
                    const uint8_t *p = view.data + static_cast<size_t>(iy) * view.stride() +
                                       static_cast<size_t>(ix) * view.channelsPerPixel();
                    QApplication::clipboard()->setText(
                        QString("#%1%2%3")
                            .arg(p[0], 2, 16, QChar('0'))
                            .arg(p[1], 2, 16, QChar('0'))
                            .arg(p[2], 2, 16, QChar('0')));
                }
            }
        }
    }
    else if (chosen == aSaveAs)
    {
        if (!m_pixmap.isNull())
        {
            const QString defaultName =
                QFileInfo(m_currentPath).completeBaseName() + "_copy.png";
            const QString path = QFileDialog::getSaveFileName(
                this, "另存为", defaultName,
                "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp);;WebP (*.webp)");
            if (!path.isEmpty())
                m_pixmap.save(path);
        }
    }
    else if (chosen == aZoomIn)
        zoomIn();
    else if (chosen == aZoomOut)
        zoomOut();
    else if (chosen == aZoomFit)
        zoomFit();
    else if (chosen == aZoomActual)
        zoomActual();
    else if (chosen == aSelectRegion)
        setSelectMode(!m_selectMode);
    else if (chosen == aNext)
        emit requestNext();
    else if (chosen == aPrev)
        emit requestPrev();
    else if (chosen == aFullscreen)
        isFullScreen() ? showNormal() : showFullScreen();
}
