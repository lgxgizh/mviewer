// Zoomed-in LOD display: schedule viewport tiles through decodeRegion so a pan
// does not wait on a full-frame materialize. The coarse display raster stays
// on screen as the underlay. TileCache keys are the usual
// (path, col, row, lod, dpr) identity.

#include "imageviewer.h"

#include "imageviewer_tile_seam.h"

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

void ImageViewer::noteTileRequest(const std::string &imageId)
{
    m_tileImageId = imageId;
    const double dpr = static_cast<double>(devicePixelRatioF());
    m_tileScalePercent =
        (std::max)(100, static_cast<int>(std::lround((std::max)(1.0, dpr) * 100.0)));
    m_requestedLod = TileCache::chooseLodStable(m_view.scale, m_pinnedLod);
}

std::vector<TileCache::ReadyTile> ImageViewer::cachedTilesForLod(int lod)
{
    std::vector<TileCache::ReadyTile> out;
    if (m_tileImageId.empty() || lod < 0 || m_tiles.tileSize <= 0)
        return out;
    const int lodSize = TileCache::lodTileSize(m_tiles.tileSize, lod);
    if (lodSize <= 0)
        return out;
    const TileGrid lodGrid(m_tiles.imageW, m_tiles.imageH, lodSize);
    const auto tiles = lodGrid.visibleTiles(m_view);
    out.reserve(tiles.size());
    for (const auto &tile : tiles)
    {
        TileKey key{m_tileImageId, tile.coord.col, tile.coord.row, lod, m_tileScalePercent};
        ImageData data = m_tileCache.get(key);
        if (!data.isNull())
            out.push_back(TileCache::ReadyTile{key, std::move(data)});
    }
    return out;
}

std::vector<TileCache::ReadyTile>
ImageViewer::composeVisibleTiles(const AsyncTileRequestManager::VisibleTiles &visible)
{
    if (visible.missing == 0)
        m_pinnedLod = m_requestedLod;
    if (visible.missing == 0 || m_pinnedLod < 0 || m_pinnedLod == m_requestedLod)
        return visible.ready;
    // A coarser level stays underneath until it covers the view, so a partial
    // result cannot replace sharp pixels. A finer level paints over the hold.
    std::vector<TileCache::ReadyTile> held = cachedTilesForLod(m_pinnedLod);
    if (m_requestedLod > m_pinnedLod)
    {
        std::vector<TileCache::ReadyTile> stacked = visible.ready;
        stacked.insert(stacked.end(), held.begin(), held.end());
        return stacked;
    }
    held.insert(held.end(), visible.ready.begin(), visible.ready.end());
    return held;
}

AsyncTileRequestManager::VisibleTiles ImageViewer::requestLodRegionTiles()
{
    const std::string path = m_currentPath.toUtf8().toStdString();
    noteTileRequest(path);
    const uint64_t generation = m_imageGeneration;
    const auto metadata =
        m_sourceImage ? m_sourceImage->metadata() : mviewer::domain::ImageMetadata{};
    const auto displayTarget = m_displayColorTarget;
    const ImageData noFullFrame;
    const int imageW = m_tiles.imageW;
    const int imageH = m_tiles.imageH;
    QPointer<ImageViewer> guard(this);
    const auto decode = [noFullFrame, metadata, displayTarget, path, imageW,
                         imageH](const std::string &, int sx, int sy, int sw, int sh, int tw,
                                 int th) -> ImageData
    {
        // Empty full frame: the reduced path must use a native or bounded
        // region. It returns null rather than decoding the whole file.
        return decodeViewerTile(path, noFullFrame, imageW, imageH, sx, sy, sw, sh, tw, th, metadata,
                                displayTarget);
    };
    return m_tileRequests.requestVisibleRegion(
        path, m_view, m_tiles, m_tileScalePercent, generation, decode,
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
