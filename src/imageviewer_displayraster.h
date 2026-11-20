#pragma once

// Viewer-internal display raster value type. Split out of imageviewer.h so the
// viewer's header stays inside its line budget (ADR-014).
//
// Include only from ImageViewer translation units; this is not a public API.

#include <QImage>
#include <QRect>
#include <QSize>

struct DisplayRaster
{
    QImage image;         // display raster (RGB), may be null
    QRect sourceRect;     // source pixels covered by the raster
    QSize sourceSize;     // full source dimensions
    double density = 1.0; // source pixels per raster pixel
};
