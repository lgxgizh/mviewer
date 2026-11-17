#pragma once
#include <QRect>
#include <QSize>
#include <algorithm>
#include <cmath>

inline bool transitionRasterNeedsSmoothing(const QSize &raster, const QSize &logicalDest,
                                           double devicePixelRatio)
{
    const double dpr = std::max(1.0, devicePixelRatio);
    return raster.width() != std::lround(logicalDest.width() * dpr) ||
           raster.height() != std::lround(logicalDest.height() * dpr);
}
