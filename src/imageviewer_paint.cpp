#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/ImageOverlay.h"
#include "core/analysis/PixelGrid.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/QtConvert.h"
#include "core/render/TileSeam.h"
#include "core/render/ZoomPercent.h"
#include "core/trace/Trace.h"
#include "gpu/GpuTileUploader.h"
#include "imageviewer_tile_seam.h"
#include "widgets/infooverlay.h"
#include "widgets/pixelgrid.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
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
#include <limits>
#include <vector>

void ImageViewer::resamplePixelUnderCursor()
{
    if (!isVisible())
        return;
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (!rect().contains(local))
        return;
    // Same cursor coordinates must not dedup away a sample of the new frame.
    m_lastHoverX = std::numeric_limits<int>::min();
    m_lastHoverY = std::numeric_limits<int>::min();
    updatePixelSampleAt(local);
}

void ImageViewer::noteDisplayedFrameForPixelReadout()
{
    const ImageFrame *frame = m_frame.get();
    if (frame == m_pixelReadoutFrame)
        return;
    m_pixelReadoutFrame = frame;
    QPointer<ImageViewer> guard(this);
    QTimer::singleShot(0, this,
                       [guard, frame]()
                       {
                           ImageViewer *viewer = guard.data();
                           if (!viewer || viewer->m_frame.get() != frame)
                               return;
                           viewer->resamplePixelUnderCursor();
                       });
}

void ImageViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    // M47: LOD-first display — a large source draws through the bounded
    // display raster (viewport LOD / visible region), never through a full
    // resolution frame. The raster is already near the screen density, so
    // this paint is cheap (no full-frame scaling on the UI thread).
    if (m_lodMode && !m_raster.image.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        drawDisplayRaster(painter);
        if (lodRegionTilesActive())
        {
            // Underlay stays the coarse raster. Missing cells come from
            // decodeRegion (native or bounded) and land in TileCache.
            ensureLodTileGrid();
            const auto regionTiles = requestLodRegionTiles();
            const Viewport tileView = m_view;
            const auto ready = composeVisibleTiles(regionTiles);
            drawGpuTiles(painter, ready, tileView);
            drawCpuTiles(painter, ready, tileView);
        }
        else if (displayNeedsUpgrade() && !m_displayUpgradeScheduled)
            scheduleDisplayUpgrade();
    }
    else if (m_frame && m_frame->isValid())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        drawProvisional(painter);
        const auto visible = requestVisibleTiles();
        if (visible.complete() && !m_provisionalImage.isNull())
        {
            m_provisionalPath.clear();
            m_provisionalImage = QImage();
            m_provisionalSourceSize = QSize();
        }
        auto ready = composeVisibleTiles(visible);
        if (ready.empty() && visible.pending > 0 && m_provisionalImage.isNull())
        {
            painter.setPen(QColor(180, 180, 180));
            painter.drawText(rect(), Qt::AlignCenter, "Loading...");
        }
        const Viewport tileView = m_view;
        scheduleOverlayTiles(ready);
        drawGpuTiles(painter, ready, tileView);
        drawCpuTiles(painter, ready, tileView);
    }
    else
        drawEmptyState(painter);

    if (m_hasHistogram)
        drawHistogram(painter);
    drawPixelGridOverlay(painter);
    drawOverlayBadge(painter);
    drawSelection(painter);
    drawFrameStatus(painter);
    if (hasDisplayImage())
    {
        const std::string zoomText = mviewer::core::formatZoomPercent(m_view.scale);
        if (!zoomText.empty())
        {
            mviewer::ui::drawZoomPercentBadge(painter, rect(),
                                              QString::fromLatin1(zoomText.c_str()), true);
        }
    }
    noteDisplayedFrameForPixelReadout();
}

void ImageViewer::drawPixelGridOverlay(QPainter &painter)
{
    int imageW = 0;
    int imageH = 0;
    if (m_lodMode && m_raster.sourceSize.isValid())
    {
        imageW = m_raster.sourceSize.width();
        imageH = m_raster.sourceSize.height();
    }
    else if (m_tiles.imageW > 0 && m_tiles.imageH > 0)
    {
        imageW = m_tiles.imageW;
        imageH = m_tiles.imageH;
    }
    if (imageW <= 0 || imageH <= 0 || !mviewer::pixelGridVisible(m_view.scale))
        return;
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(0, 0, imageW, imageH, sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    mviewer::ui::drawPixelGrid(painter, QRectF(sx, sy, sw, sh), 0, 0, imageW, imageH,
                               QRectF(rect()));
}

void ImageViewer::drawOverlayBadge(QPainter &painter)
{
    if (m_overlayMode == mviewer::OverlayMode::None)
        return;
    const QString label = QString::fromLatin1(mviewer::overlayModeLabel(m_overlayMode));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int pad = 6;
    const QRect box(8, 8, metrics.horizontalAdvance(label) + pad * 2, metrics.height() + pad);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(box, 3, 3);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, label);
}

void ImageViewer::drawFrameStatus(QPainter &painter) const
{
    if (m_frameStatusText.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics metrics(painter.font());
    const int availableWidth = std::max(80, width() - 24);
    const QString text =
        metrics.elidedText(m_frameStatusText, Qt::ElideRight, std::max(40, availableWidth - 20));
    const int pillWidth = std::min(availableWidth, metrics.horizontalAdvance(text) + 20);
    const int pillHeight = metrics.height() + 12;
    const QRect pill(12, std::max(12, height() - pillHeight - 12), pillWidth, pillHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(pill, 6, 6);
    painter.setPen(Qt::white);
    painter.drawText(pill.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    painter.restore();
}

void ImageViewer::drawProvisional(QPainter &painter) const
{
    if (m_provisionalImage.isNull())
        return;
    const int sourceW = m_provisionalSourceSize.width() > 0 ? m_provisionalSourceSize.width()
                                                            : m_provisionalImage.width();
    const int sourceH = m_provisionalSourceSize.height() > 0 ? m_provisionalSourceSize.height()
                                                             : m_provisionalImage.height();
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(0, 0, sourceW, sourceH, sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    const QRect targetRect(sx, sy, sw, sh);
    const QRect viewportRect(0, 0, m_view.screenW, m_view.screenH);
    if (!targetRect.intersects(viewportRect))
        return;

    const QRect visibleTarget = targetRect.intersected(viewportRect);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (visibleTarget == targetRect)
    {
        painter.drawImage(targetRect, m_provisionalImage);
    }
    else
    {
        const double scaleX = static_cast<double>(m_provisionalImage.width()) / sw;
        const double scaleY = static_cast<double>(m_provisionalImage.height()) / sh;
        const QRectF sourceSubRect((visibleTarget.left() - sx) * scaleX,
                                   (visibleTarget.top() - sy) * scaleY,
                                   visibleTarget.width() * scaleX, visibleTarget.height() * scaleY);
        painter.drawImage(visibleTarget, m_provisionalImage, sourceSubRect);
    }
    painter.restore();
}

QImage ImageViewer::ownedTransitionSnapshot(const QImage &view) const
{
    if (view.isNull())
        return {};
    // toQImageRef() does not own its buffer, and the frame is released as soon
    // as this load replaces it. A window-sized copy stays valid after that and
    // does not memcpy a full photo on the UI thread while the user flips.
    int edge = width() > height() ? width() : height();
    if (edge < 64)
        edge = 64;
    if (view.width() <= edge && view.height() <= edge)
        return view.copy();
    return view.scaled(edge, edge, Qt::KeepAspectRatio, Qt::FastTransformation);
}

void ImageViewer::drawTransition(QPainter &painter) const
{
    if (m_transitionImage.isNull())
        return;
    const int sourceW = m_transitionSourceSize.width() > 0 ? m_transitionSourceSize.width()
                                                           : m_transitionImage.width();
    const int sourceH = m_transitionSourceSize.height() > 0 ? m_transitionSourceSize.height()
                                                            : m_transitionImage.height();
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(0, 0, sourceW, sourceH, sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    const QRect targetRect(sx, sy, sw, sh);
    const QRect viewportRect(0, 0, m_view.screenW, m_view.screenH);
    if (!targetRect.intersects(viewportRect))
        return;

    const QRect visibleTarget = targetRect.intersected(viewportRect);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (visibleTarget == targetRect)
    {
        painter.drawImage(targetRect, m_transitionImage);
    }
    else
    {
        const double scaleX = static_cast<double>(m_transitionImage.width()) / sw;
        const double scaleY = static_cast<double>(m_transitionImage.height()) / sh;
        const QRectF sourceSubRect((visibleTarget.left() - sx) * scaleX,
                                   (visibleTarget.top() - sy) * scaleY,
                                   visibleTarget.width() * scaleX, visibleTarget.height() * scaleY);
        painter.drawImage(visibleTarget, m_transitionImage, sourceSubRect);
    }
    painter.restore();
}

AsyncTileRequestManager::VisibleTiles ImageViewer::requestVisibleTiles()
{
    MV_TRACE_SCOPED("ImageViewer::paint");
    const std::string id = m_frame->id().hash;
    noteTileRequest(id);
    const uint64_t generation = m_imageGeneration;
    const ImageData source = m_frame->pixels();
    const auto metadata = m_frame->metadata();
    const auto displayTarget = m_displayColorTarget;
    const std::string path =
        !metadata.filePath.empty() ? metadata.filePath : m_currentPath.toUtf8().toStdString();
    const int imageW = m_tiles.imageW;
    const int imageH = m_tiles.imageH;
    QPointer<ImageViewer> guard(this);
    const auto decode = [source, metadata, displayTarget, path, imageW,
                         imageH](const std::string &, int sx, int sy, int sw, int sh, int tw,
                                 int th) -> ImageData
    {
        return decodeViewerTile(path, source, imageW, imageH, sx, sy, sw, sh, tw, th, metadata,
                                displayTarget);
    };
    return m_tileRequests.requestVisibleRegion(
        id, m_view, m_tiles, m_tileScalePercent, generation, decode,
        [guard, generation](const TileKey &)
        {
            if (!guard || !qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, generation]()
                {
                    ImageViewer *viewer = guard.data();
                    if (!viewer || generation != viewer->m_imageGeneration ||
                        viewer->m_tileRepaintQueued)
                        return;
                    viewer->m_tileRepaintQueued = true;
                    QTimer::singleShot(0, viewer,
                                       [guard, generation]()
                                       {
                                           ImageViewer *current = guard.data();
                                           if (!current)
                                               return;
                                           current->m_tileRepaintQueued = false;
                                           if (generation != current->m_imageGeneration)
                                               return;
                                           current->update();
                                       });
                },
                Qt::QueuedConnection);
        },
        m_requestedLod);
}

void ImageViewer::scheduleOverlayTiles(std::vector<TileCache::ReadyTile> &ready)
{
    if (m_overlayMode == mviewer::OverlayMode::None)
        return;
    for (auto &rt : ready)
    {
        ImageData derived = m_overlayCache.get(rt.key);
        if (derived.isNull())
        {
            const auto mode = m_overlayMode;
            const int threshold = m_zebraThreshold;
            QPointer<ImageViewer> guard(this);
            m_overlayRequests.requestDerived(
                rt.key, rt.data, m_overlayGeneration,
                [mode, threshold](const TileKey &, const ImageData &source)
                {
                    ImageData value = makeImageData(source.width, source.height, source.format);
                    if (value.isNull())
                        return value;
                    std::memcpy(value.buffer->data(), source.buffer->data(), source.byteSize());
                    mviewer::applyOverlay(value, mode, threshold);
                    return value;
                },
                [guard, generation = m_overlayGeneration](const TileKey &)
                {
                    if (!guard || !qApp)
                        return;
                    QMetaObject::invokeMethod(
                        qApp,
                        [guard, generation]()
                        {
                            ImageViewer *viewer = guard.data();
                            if (!viewer || generation != viewer->m_overlayGeneration ||
                                viewer->m_tileRepaintQueued)
                                return;
                            viewer->m_tileRepaintQueued = true;
                            QTimer::singleShot(0, viewer,
                                               [guard, generation]()
                                               {
                                                   ImageViewer *current = guard.data();
                                                   if (!current)
                                                       return;
                                                   current->m_tileRepaintQueued = false;
                                                   if (generation != current->m_overlayGeneration)
                                                       return;
                                                   current->update();
                                               });
                        },
                        Qt::QueuedConnection);
                });
        }
        else
            rt.data = std::move(derived);
    }
}

namespace
{

mviewer::core::TileScreenPlacement placedTile(const Viewport &tileView, const TileGrid &tiles,
                                              const TileCache::ReadyTile &rt, int decodedW,
                                              int decodedH)
{
    return mviewer::core::placeTile(tileView, tiles.imageW, tiles.imageH, tiles.tileSize,
                                    rt.key.col, rt.key.row, rt.key.lod, rt.key.renderScalePercent,
                                    decodedW, decodedH);
}

void blitPlacedTile(QOpenGLTextureBlitter &blitter, GpuTileUploader &gpu, uintptr_t handle,
                    const mviewer::core::TileScreenPlacement &placed, const QRect &viewportRect)
{
    if (!placed.valid || handle == 0)
        return;
    const bool magnified = placed.screenW > placed.contentW || placed.screenH > placed.contentH;
    gpu.setMagnifyNearest(handle, magnified);
    const QRect destination(placed.screenX, placed.screenY, placed.screenW, placed.screenH);
    const QMatrix4x4 target = QOpenGLTextureBlitter::targetTransform(destination, viewportRect);
    const bool fullTexture = placed.contentX == 0 && placed.contentY == 0 &&
                             placed.contentW == placed.texW && placed.contentH == placed.texH;
    if (fullTexture)
    {
        blitter.blit(static_cast<GLuint>(handle), target, QOpenGLTextureBlitter::OriginTopLeft);
        return;
    }
    const QMatrix3x3 source = QOpenGLTextureBlitter::sourceTransform(
        QRectF(placed.contentX, placed.contentY, placed.contentW, placed.contentH),
        QSize(placed.texW, placed.texH), QOpenGLTextureBlitter::OriginTopLeft);
    blitter.blit(static_cast<GLuint>(handle), target, source);
}

void drawPlacedCpuTile(QPainter &painter, const QImage &image,
                       const mviewer::core::TileScreenPlacement &placed)
{
    if (!placed.valid || image.isNull())
        return;
    painter.save();
    const bool magnified = placed.screenW > placed.contentW || placed.screenH > placed.contentH;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !magnified);
    const QRect screen(placed.screenX, placed.screenY, placed.screenW, placed.screenH);
    const bool apron = placed.contentX != 0 || placed.contentY != 0 ||
                       placed.contentW != placed.texW || placed.contentH != placed.texH;
    if (!apron)
    {
        painter.drawImage(screen, image);
        painter.restore();
        return;
    }
    const double scaleX =
        static_cast<double>(placed.screenW) / static_cast<double>(placed.contentW);
    const double scaleY =
        static_cast<double>(placed.screenH) / static_cast<double>(placed.contentH);
    const QRectF dest(
        placed.screenX - placed.contentX * scaleX, placed.screenY - placed.contentY * scaleY,
        static_cast<double>(placed.texW) * scaleX, static_cast<double>(placed.texH) * scaleY);
    painter.setClipRect(screen, Qt::IntersectClip);
    painter.drawImage(dest, image);
    painter.restore();
}

} // namespace

void ImageViewer::drawGpuTiles(QPainter &painter, const std::vector<TileCache::ReadyTile> &ready,
                               const Viewport &tileView)
{
    const bool useGpu =
        GpuTileUploader::enabled() && m_blitterReady && m_overlayMode == mviewer::OverlayMode::None;
    if (!useGpu)
        return;
    const QRect viewportRect(0, 0, width(), height());
    std::vector<TileKey> pinned;
    pinned.reserve(ready.size());
    for (const auto &rt : ready)
    {
        if (!rt.data.isNull())
            pinned.push_back(rt.key);
    }
    m_gpu.pinVisible(pinned.data(), pinned.size());
    painter.beginNativePainting();
    for (const auto &rt : ready)
    {
        if (rt.data.isNull())
            continue;
        const ImageBuffer view = rt.data.view();
        m_gpu.ensure(rt.key, view.data, view.width, view.height, view.channelsPerPixel());
    }
    m_blitter.bind();
    for (const auto &rt : ready)
    {
        if (rt.data.isNull())
            continue;
        const auto handle = m_gpu.handle(rt.key);
        const auto placed = placedTile(tileView, m_tiles, rt, rt.data.width, rt.data.height);
        blitPlacedTile(m_blitter, m_gpu, handle, placed, viewportRect);
    }
    m_blitter.release();
    painter.endNativePainting();
}

void ImageViewer::drawCpuTiles(QPainter &painter, const std::vector<TileCache::ReadyTile> &ready,
                               const Viewport &tileView)
{
    const bool gpuActive =
        GpuTileUploader::enabled() && m_blitterReady && m_overlayMode == mviewer::OverlayMode::None;
    for (const auto &rt : ready)
    {
        if (rt.data.isNull())
            continue;
        if (gpuActive && m_gpu.handle(rt.key) != 0)
            continue;
        QImage image = mvcore::toQImageRef(rt.data);
        if (image.isNull())
            image = mvcore::toQImage(rt.data);
        const auto placed = placedTile(tileView, m_tiles, rt, image.width(), image.height());
        drawPlacedCpuTile(painter, image, placed);
    }
}

void ImageViewer::drawDisplayRaster(QPainter &painter) const
{
    if (m_raster.image.isNull())
        return;
    const QRect &r = m_raster.sourceRect;
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(r.x(), r.y(), r.width(), r.height(), sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    const QImage *drawn = &m_raster.image;
    if (m_overlayMode != mviewer::OverlayMode::None)
    {
        const qint64 key = m_raster.image.cacheKey();
        const bool ready = m_lodOverlayKey == key && m_lodOverlayMode == m_overlayMode &&
                           m_lodOverlayThreshold == m_zebraThreshold && !m_lodOverlayImage.isNull();
        if (ready)
            drawn = &m_lodOverlayImage;
        else
            scheduleLodOverlayDerivation();
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(sx, sy, sw, sh), *drawn);
    painter.restore();
}

void ImageViewer::drawEmptyState(QPainter &painter)
{
    if (!m_provisionalImage.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        drawProvisional(painter);
        return;
    }
    if (m_loading)
    {
        if (!m_transitionImage.isNull())
        {
            m_view.screenW = width();
            m_view.screenH = height();
            drawTransition(painter);
            return;
        }
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, "加载中…");
        return;
    }
    if (!m_currentPath.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "无法加载图片");
        return;
    }
    painter.setPen(QColor(180, 180, 180));
    QFont font = painter.font();
    font.setPointSize(font.pointSize() + 2);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter,
                     "拖放图片或文件夹到此处\n"
                     "或按 Ctrl+O 打开目录\n"
                     "或双击缩略图查看");
}

void ImageViewer::drawSelection(QPainter &painter)
{
    if (!m_selectMode)
        return;
    const QRect region = m_selecting ? QRect(m_selStart, m_selEnd).normalized() : selectedRegion();
    if (!region.isValid() || (!m_selecting && region.width() <= 5) ||
        (!m_selecting && region.height() <= 5))
        return;
    painter.save();
    painter.setPen(QPen(QColor(255, 255, 0), 1));
    painter.setBrush(QColor(255, 255, 0, 80));
    painter.drawRect(region);
    if (m_frame && m_frame->width() > 0)
    {
        const int imageWidth = static_cast<int>(region.width() / m_view.scale);
        const int imageHeight = static_cast<int>(region.height() / m_view.scale);
        if (imageWidth > 0 && imageHeight > 0)
        {
            const QString sizeText = QString("%1×%2").arg(imageWidth).arg(imageHeight);
            painter.setPen(QColor(255, 255, 0));
            QFont font = painter.font();
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(region.bottomRight() + QPoint(8, 14), sizeText);
        }
    }
    painter.restore();
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
