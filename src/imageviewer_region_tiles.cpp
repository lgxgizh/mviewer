// Zoomed-in LOD display: schedule viewport tiles through decodeRegion so a pan
// does not wait on a full-frame materialize. The coarse display raster stays
// on screen as the underlay. TileCache keys are the usual
// (path, col, row, lod, dpr) identity.

#include "imageviewer.h"

#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/render/RenderEngine.h"
#include "core/render/TileSourceDecode.h"

#include <QApplication>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

bool ImageViewer::lodRegionTilesActive() const
{
    if (!m_lodMode || m_raster.image.isNull() || !m_sourceImage || !m_sourceImage->isValid())
        return false;
    if (!(m_view.scale >= 1.0) || !std::isfinite(m_view.scale))
        return false;
    const auto path = m_sourceImage->regionDecodePath();
    return path == mviewer::core::SourceDecodePath::NativeRegion ||
           path == mviewer::core::SourceDecodePath::BoundedRasterRegion;
}

void ImageViewer::ensureLodTileGrid()
{
    const int w = m_raster.sourceSize.width();
    const int h = m_raster.sourceSize.height();
    if (w <= 0 || h <= 0)
        return;
    if (m_tiles.imageW == w && m_tiles.imageH == h && m_tiles.tileSize > 0)
        return;
    m_tiles = TileGrid(w, h, 256);
}

AsyncTileRequestManager::VisibleTiles ImageViewer::requestLodRegionTiles()
{
    const std::string path = m_currentPath.toUtf8().toStdString();
    const qreal dpr = devicePixelRatioF();
    const int renderScalePercent =
        std::max(100, static_cast<int>(std::lround(std::max<qreal>(1.0, dpr) * 100.0)));
    const uint64_t generation = m_imageGeneration;
    const auto metadata =
        m_sourceImage ? m_sourceImage->metadata() : mviewer::domain::ImageMetadata{};
    const auto displayTarget = m_displayColorTarget;
    const ImageData noFullFrame;
    QPointer<ImageViewer> guard(this);
    const auto decode = [noFullFrame, metadata, displayTarget, path](const std::string &, int sx,
                                                                     int sy, int sw, int sh, int tw,
                                                                     int th) -> ImageData
    {
        // Empty full frame: decodeTilePreferReduced must use native region or
        // a bounded crop. It returns null rather than decoding the whole file.
        const ImageData raw =
            mviewer::core::decodeTilePreferReduced(path, noFullFrame, sx, sy, sw, sh, tw, th);
        if (raw.isNull())
            return {};
        return mvcore::toDisplayImageData(raw, metadata, displayTarget);
    };
    return m_tileRequests.requestVisibleRegion(
        path, m_view, m_tiles, renderScalePercent, generation, decode,
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
        });
}
