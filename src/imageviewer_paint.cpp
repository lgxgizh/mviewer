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
        // F4 (M22): apply the live overlay on a deep copy so the TileCache
        // buffer (shared via shared_ptr) is never mutated. Without the copy,
        // toggling the overlay off would leave the cached pixels clipped.
        if (m_overlayMode != mviewer::OverlayMode::None)
        {
            for (auto &rt : ready)
            {
                ImageData out = makeImageData(rt.data.width, rt.data.height, rt.data.format);
                std::memcpy(out.buffer->data(), rt.data.buffer->data(), rt.data.byteSize());
                mviewer::applyOverlay(out, m_overlayMode, m_zebraThreshold);
                rt.data = out;
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
                if (dpr > 1.0)
                {
                    tsx = static_cast<int>(std::lround(tsx / dpr));
                    tsy = static_cast<int>(std::lround(tsy / dpr));
                    tsw = static_cast<int>(std::lround(tsw / dpr));
                    tsh = static_cast<int>(std::lround(tsh / dpr));
                }
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
