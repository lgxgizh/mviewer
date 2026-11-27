#pragma once

// Viewer-internal display raster value types. Split out of imageviewer.h so the
// viewer's header stays inside its line budget (ADR-014).
//
// Include only from ImageViewer translation units; this is not a public API.

#include "application/ImageLoadingService.h"
#include "core/image/DisplayColorContext.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>

struct DisplayRaster
{
    QImage image;         // display raster (RGB), may be null
    QRect sourceRect;     // source pixels covered by the raster
    QSize sourceSize;     // full source dimensions
    double density = 1.0; // source pixels per raster pixel
};

/// Bookkeeping for one in-flight neighbor display preload. Kept global (rather
/// than nested in ImageViewer) so imageviewer.h can drop ~45 lines; ImageViewer
/// keeps `using` aliases so existing call sites read unchanged.
struct DisplayRasterPreloadState
{
    std::atomic<uint64_t> promotedGeneration{0};
    std::atomic<uint64_t> displaySerial{0}; // serial this preload was promoted into
    std::atomic<bool> delivered{false};
    int widgetW = 0, widgetH = 0;
    double dpr = 1.0;
    double lockedScale = 0.0; // >0: zoom-lock scale, else fit the neighbor
    bool maximizeFit = false;
};

struct DisplayRasterPreloadResult
{
    QString path;
    uint64_t browseGeneration = 0;
    std::shared_ptr<DisplayRasterPreloadState> state;
    std::shared_ptr<mviewer::core::SourceImage> source;
    QImage image;
    QRect sourceRect;
    QSize sourceSize;
    double density = 1.0;
    mviewer::core::DisplayColorContext target;
    bool failed = false;
};

struct DisplayRasterWarm
{
    QString path;
    std::shared_ptr<mviewer::core::SourceImage> source;
    QImage image;
    QRect sourceRect;
    QSize sourceSize;
    double density = 1.0;
    mviewer::core::DisplayColorContext target;
    qint64 bytes = 0;
    uint64_t lastUse = 0;
};

struct DisplayRasterPreload
{
    QString path;
    std::shared_ptr<DisplayRasterPreloadState> state;
    TaskScheduler::TaskHandle handle;
};

/// A browse neighbor being decoded ahead of navigation (not a display raster).
struct NeighborPreload
{
    QString path;
    mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
};
