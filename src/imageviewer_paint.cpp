#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
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
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMetaObject>
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
        if (displayNeedsUpgrade() && !m_displayUpgradeScheduled)
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
        auto ready = visible.ready;
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
    drawSelection(painter);
    drawFrameStatus(painter);
}

void ImageViewer::drawFrameStatus(QPainter &painter) const
{
    if (m_frameStatusText.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics metrics(painter.font());
    const int availableWidth = std::max(80, width() - 24);
    const QString text = metrics.elidedText(m_frameStatusText, Qt::ElideRight,
                                             std::max(40, availableWidth - 20));
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
    const int sourceW = m_provisionalSourceSize.width() > 0
                            ? m_provisionalSourceSize.width()
                            : m_provisionalImage.width();
    const int sourceH = m_provisionalSourceSize.height() > 0
                            ? m_provisionalSourceSize.height()
                            : m_provisionalImage.height();
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(0, 0, sourceW, sourceH, sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(sx, sy, sw, sh), m_provisionalImage);
    painter.restore();
}

AsyncTileRequestManager::VisibleTiles ImageViewer::requestVisibleTiles()
{
    MV_TRACE_SCOPED("ImageViewer::paint");
    const std::string id = m_frame->id().hash;
    const qreal dpr = devicePixelRatioF();
    const int renderScalePercent =
        std::max(100, static_cast<int>(std::lround(std::max<qreal>(1.0, dpr) * 100.0)));
    const uint64_t generation = m_imageGeneration;
    const ImageData source = m_frame->pixels();
    const auto metadata = m_frame->metadata();
    QPointer<ImageViewer> guard(this);
    const auto decode = [source, metadata](const std::string &, int sx, int sy, int sw, int sh,
                                            int tw, int th) -> ImageData
    {
        const ImageData raw = RenderEngine::scaleRegionStatic(
            source, RenderRect{sx, sy, sw, sh}, RenderSize{tw, th}, RenderInterp::Bilinear);
        return mvcore::toDisplayImageData(raw, metadata);
    };
    return m_tileRequests.requestVisible(
        id, m_view, m_tiles, renderScalePercent, generation, decode,
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
                    QTimer::singleShot(
                        0, viewer,
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
        });
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
                            QTimer::singleShot(
                                0, viewer,
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

void ImageViewer::drawGpuTiles(QPainter &painter,
                               const std::vector<TileCache::ReadyTile> &ready,
                               const Viewport &tileView)
{
    const bool useGpu = GpuTileUploader::enabled() && m_blitterReady &&
                        m_overlayMode == mviewer::OverlayMode::None;
    if (!useGpu)
        return;
    const QRect viewportRect(0, 0, width(), height());
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
        const auto handle = m_gpu.handle(rt.key);
        if (handle == 0)
            continue;
        int screenX = 0;
        int screenY = 0;
        int screenW = 0;
        int screenH = 0;
        const int lod = rt.key.lod;
        const int sourceX = rt.key.col * m_tiles.tileSize * (1 << lod);
        const int sourceY = rt.key.row * m_tiles.tileSize * (1 << lod);
        const int lodSize = TileCache::lodTileSize(m_tiles.tileSize, lod);
        const int actualW = qMin(lodSize, m_tiles.imageW - sourceX);
        const int actualH = qMin(lodSize, m_tiles.imageH - sourceY);
        if (actualW <= 0 || actualH <= 0)
            continue;
        tileView.imageRectToScreen(sourceX, sourceY, actualW, actualH,
                                   screenX, screenY, screenW, screenH);
        const QRect destination(screenX, screenY, screenW, screenH);
        const QMatrix4x4 target =
            QOpenGLTextureBlitter::targetTransform(destination, viewportRect);
        m_blitter.blit(static_cast<GLuint>(handle), target,
                       QOpenGLTextureBlitter::OriginTopLeft);
    }
    m_blitter.release();
    painter.endNativePainting();
}

void ImageViewer::drawCpuTiles(QPainter &painter,
                               const std::vector<TileCache::ReadyTile> &ready,
                               const Viewport &tileView)
{
    const bool gpuActive = GpuTileUploader::enabled() && m_blitterReady &&
                           m_overlayMode == mviewer::OverlayMode::None;
    for (const auto &rt : ready)
    {
        if (gpuActive && m_gpu.handle(rt.key) != 0)
            continue;
        int screenX = 0;
        int screenY = 0;
        int screenW = 0;
        int screenH = 0;
        const int lod = rt.key.lod;
        const int sourceX = rt.key.col * m_tiles.tileSize * (1 << lod);
        const int sourceY = rt.key.row * m_tiles.tileSize * (1 << lod);
        const int lodSize = TileCache::lodTileSize(m_tiles.tileSize, lod);
        const int actualW = qMin(lodSize, m_tiles.imageW - sourceX);
        const int actualH = qMin(lodSize, m_tiles.imageH - sourceY);
        if (actualW <= 0 || actualH <= 0)
            continue;
        tileView.imageRectToScreen(sourceX, sourceY, actualW, actualH,
                                   screenX, screenY, screenW, screenH);
        QImage image = mvcore::toQImageRef(rt.data);
        if (image.isNull())
            image = mvcore::toQImage(rt.data);
        if (!image.isNull())
            painter.drawImage(QRect(screenX, screenY, screenW, screenH), image);
    }
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
    if (m_selecting && m_frame && m_frame->width() > 0)
    {
        const int imageWidth = static_cast<int>(region.width() / m_view.scale);
        const int imageHeight = static_cast<int>(region.height() / m_view.scale);
        const QString sizeText = QString("%1×%2").arg(imageWidth).arg(imageHeight);
        painter.setPen(QColor(255, 255, 0));
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(region.bottomRight() + QPoint(8, 14), sizeText);
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
