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

    const auto drawProvisional = [&]()
    {
        if (m_provisionalImage.isNull())
            return;
        const int sourceW = m_provisionalSourceSize.width() > 0
                                ? m_provisionalSourceSize.width()
                                : m_provisionalImage.width();
        const int sourceH = m_provisionalSourceSize.height() > 0
                                ? m_provisionalSourceSize.height()
                                : m_provisionalImage.height();
        int sx = 0, sy = 0, sw = 0, sh = 0;
        m_view.imageRectToScreen(0, 0, sourceW, sourceH, sx, sy, sw, sh);
        if (sw <= 0 || sh <= 0)
            return;
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QRect(sx, sy, sw, sh), m_provisionalImage);
        painter.restore();
    };

    // Render Pipeline (P1-①): the Widget is the Viewport owner. It asks the
    // TileGrid which source tiles are visible, then asks the Renderer to scale
    // each visible region from the ImageFrame pixels — never decoding, never
    // rasterizing the whole image into one bitmap. This is what makes 100MP/
    // RAW rendering feasible later.
    if (m_frame && m_frame->isValid())
    {
        MV_TRACE_SCOPED("ImageViewer::paint");
        // Keep Viewport in *logical* widget pixels so mouse pan/zoom math stays
        // consistent. HiDPI sharpness is handled by decoding tiles at device
        // resolution (see dpr scale below) without changing interaction space.
        m_view.screenW = width();
        m_view.screenH = height();
        drawProvisional();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // Render Pipeline (P1-①): ask the TileCache for the visible tiles at
        // the LOD chosen for the current zoom. Only missing tiles are decoded
        // (via RenderEngine::scaleRegion in core/), then cached. The Widget
        // never decodes and never rasterizes the whole image.
        const std::string id = m_frame->id().hash;
        const qreal dpr = devicePixelRatioF();
        const int renderScalePercent =
            std::max(100, static_cast<int>(std::lround(std::max<qreal>(1.0, dpr) * 100.0)));
        const uint64_t generation = m_imageGeneration;
        const ImageData source = m_frame->pixels();
        const auto metadata = m_frame->metadata();
        QPointer<ImageViewer> guard(this);
        const auto decode = [source, metadata](const std::string &, int sx, int sy, int sw,
                                                int sh, int tw, int th) -> ImageData
        {
            const ImageData raw = RenderEngine::scaleRegionStatic(
                source, RenderRect{sx, sy, sw, sh}, RenderSize{tw, th}, RenderInterp::Bilinear);
            return mvcore::toDisplayImageData(raw, metadata);
        };
        const auto visible = m_tileRequests.requestVisible(
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
        if (visible.complete() && !m_provisionalImage.isNull())
        {
            m_provisionalPath.clear();
            m_provisionalImage = QImage();
            m_provisionalSourceSize = QSize();
        }
        std::vector<TileCache::ReadyTile> ready = visible.ready;
        if (ready.empty() && visible.pending > 0 && m_provisionalImage.isNull())
        {
            painter.setPen(QColor(180, 180, 180));
            painter.drawText(rect(), Qt::AlignCenter, "Loading...");
        }
        const Viewport tileView = m_view;

        // F4/M39: derived overlay pixels are materialized off the GUI thread.
        // The paint path only consumes a ready derived tile and draws the base
        // tile provisionally while the bounded async request is in flight.
        if (m_overlayMode != mviewer::OverlayMode::None)
        {
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
                            ImageData value =
                                makeImageData(source.width, source.height, source.format);
                            if (value.isNull())
                                return value;
                            std::memcpy(value.buffer->data(), source.buffer->data(),
                                        source.byteSize());
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
                {
                    rt.data = std::move(derived);
                }
            }
        }
        // Stage A: QOpenGLWidget keeps a GL context current during paintEvent,
        // so GpuTileUploader::enabled() can succeed when MVIEWER_GPU=1.
        const bool useGpu = GpuTileUploader::enabled() && m_blitterReady &&
                            m_overlayMode == mviewer::OverlayMode::None;
        const QRect viewportRect(0, 0, width(), height());

        // Upload + GPU composite phase (native GL). Must not interleave with
        // QPainter draws — beginNativePainting brackets all GL work.
        if (useGpu)
        {
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
                const auto hnd = m_gpu.handle(rt.key);
                if (hnd == 0)
                    continue;
                int tsx, tsy, tsw, tsh;
                // Use the ACTUAL tile extent (clamped to image bounds). The cached
                // tile buffer only covers the real source region, so sizing the
                // destination by the nominal lodTileSize would stretch the trailing
                // partial tile sideways (visible as a block "extending right").
                const int lodA = rt.key.lod;
                const int srcXA = rt.key.col * m_tiles.tileSize * (1 << lodA);
                const int srcYA = rt.key.row * m_tiles.tileSize * (1 << lodA);
                const int lodSizeA = TileCache::lodTileSize(m_tiles.tileSize, lodA);
                const int actualWA = qMin(lodSizeA, m_tiles.imageW - srcXA);
                const int actualHA = qMin(lodSizeA, m_tiles.imageH - srcYA);
                if (actualWA <= 0 || actualHA <= 0)
                    continue;
                tileView.imageRectToScreen(srcXA, srcYA, actualWA, actualHA, tsx, tsy, tsw, tsh);
                const QRect dst(tsx, tsy, tsw, tsh);
                const QMatrix4x4 target = QOpenGLTextureBlitter::targetTransform(dst, viewportRect);
                m_blitter.blit(static_cast<GLuint>(hnd), target,
                               QOpenGLTextureBlitter::OriginTopLeft);
            }
            m_blitter.release();
            painter.endNativePainting();
        }

        // CPU fallback for tiles that are not GPU-resident (or GPU off).
        for (const auto &rt : ready)
        {
            if (useGpu && m_gpu.handle(rt.key) != 0)
                continue; // already drawn via blitter
            int tsx, tsy, tsw, tsh;
            // Same clamped-extent fix as the GPU path: draw only the real tile
            // region so the last partial tile is not stretched to fill a
            // nominal lodTileSize destination.
            const int lodA = rt.key.lod;
            const int srcXA = rt.key.col * m_tiles.tileSize * (1 << lodA);
            const int srcYA = rt.key.row * m_tiles.tileSize * (1 << lodA);
            const int lodSizeA = TileCache::lodTileSize(m_tiles.tileSize, lodA);
            const int actualWA = qMin(lodSizeA, m_tiles.imageW - srcXA);
            const int actualHA = qMin(lodSizeA, m_tiles.imageH - srcYA);
            if (actualWA <= 0 || actualHA <= 0)
                continue;
            tileView.imageRectToScreen(srcXA, srcYA, actualWA, actualHA, tsx, tsy, tsw, tsh);
            // ReadyTile owns the shared ImageData for the duration of this
            // draw call, so supported byte layouts can be exposed as a
            // non-owning QImage view. RGBA32 (and any future unsupported
            // layout) keeps the explicit owned-conversion fallback.
            QImage q = mvcore::toQImageRef(rt.data);
            if (q.isNull())
                q = mvcore::toQImage(rt.data);
            if (q.isNull())
                continue;
            painter.drawImage(QRect(tsx, tsy, tsw, tsh), q);
        }
    }
    else if (!m_provisionalImage.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        drawProvisional();
    }
    else if (m_loading)
    {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, "加载中…");
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
            if (m_selecting && m_frame && m_frame->width() > 0)
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
