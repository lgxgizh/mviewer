//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-Micense-Identifier: MIT
//
// Histogram — domain-free RGB histogram computation for compare/inspection.
// No Qt dependency; operates directly on decoded ImageData.
//
#pragma once

#include <algorithm>
#include <vector>

#include "core/image/ImageBuffer.h"

namespace mviewer::core
{

// Per-channel 256-bin luminance/colour histogram of a decoded image.
// M23: adds a Rec.601 luma channel alongside R/G/B.
struct Histogram
{
    std::vector<long> r, g, b;
    std::vector<long> luma; // Rec.601: 0.299 R + 0.587 G + 0.114 B
    int bins = 256;
    long total = 0;
};

// M23 (ROI + Histogram 联动): compute an RGB+luma histogram over a rectangular
// region of @p img (image coordinates, clipped to bounds). A degenerate ROI
// (w/h <= 0 or fully outside) yields an all-zero histogram.
// Handles RGB24 / BGR24 / RGBA32 / BGRA32 / Grayscale8 layouts.
inline Histogram computeHistogram(const ImageData &img, int roiX, int roiY, int roiW, int roiH,
                                  int bins = 256, int stepX = 1, int stepY = 1)
{
    Histogram h;
    h.bins = std::max(1, bins);
    h.r.assign(static_cast<size_t>(h.bins), 0);
    h.g.assign(static_cast<size_t>(h.bins), 0);
    h.b.assign(static_cast<size_t>(h.bins), 0);
    h.luma.assign(static_cast<size_t>(h.bins), 0);

    if (img.isNull() || roiW <= 0 || roiH <= 0)
        return h;

    const ImageBuffer v = img.view();
    const int x0 = std::max(0, roiX);
    const int y0 = std::max(0, roiY);
    const int x1 = std::min(v.width, roiX + roiW);
    const int y1 = std::min(v.height, roiY + roiH);
    if (x0 >= x1 || y0 >= y1)
        return h;

    const int sx = std::max(1, stepX);
    const int sy = std::max(1, stepY);
    const int cpp = v.channelsPerPixel();
    const long stride = static_cast<long>(v.stride());
    const uint8_t *data = v.data;

    const bool gray = (v.format == PixelFormat::Grayscale8);
    const bool bgr = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);

    long samples = 0;
    for (int y = y0; y < y1; y += sy)
    {
        const uint8_t *row = data + static_cast<ptrdiff_t>(y) * stride;
        for (int x = x0; x < x1; x += sx)
        {
            const uint8_t *p = row + static_cast<ptrdiff_t>(x) * cpp;
            int R, G, B;
            if (gray)
            {
                R = G = B = p[0];
            }
            else if (bgr)
            {
                B = p[0];
                G = p[1];
                R = p[2];
            }
            else
            {
                R = p[0];
                G = p[1];
                B = p[2];
            }
            h.r[std::min<int>(R, h.bins - 1)]++;
            h.g[std::min<int>(G, h.bins - 1)]++;
            h.b[std::min<int>(B, h.bins - 1)]++;
            // Rec.601 luma via the ONE shared implementation (ImageBuffer.h).
            // The previous local copy rounded (+0.5) while ImageFrame truncated,
            // so Browse and Compare reported different luma bins for one image.
            const int Y = luminance(static_cast<uint8_t>(R), static_cast<uint8_t>(G),
                                    static_cast<uint8_t>(B));
            h.luma[std::min<int>(Y, h.bins - 1)]++;
            ++samples;
        }
    }
    h.total = samples;
    return h;
}

// Compute an RGB+luma histogram over every pixel of @p img.
// Returns an empty histogram (all zeros) when @p img is null.
inline Histogram computeHistogram(const ImageData &img, int bins = 256)
{
    if (img.isNull())
    {
        Histogram h;
        h.bins = std::max(1, bins);
        h.r.assign(static_cast<size_t>(h.bins), 0);
        h.g.assign(static_cast<size_t>(h.bins), 0);
        h.b.assign(static_cast<size_t>(h.bins), 0);
        h.luma.assign(static_cast<size_t>(h.bins), 0);
        return h;
    }
    return computeHistogram(img, 0, 0, img.width, img.height, bins);
}

// Overlay / inspection histogram: subsample so the long edge is about
// kDisplayHistogramMaxEdge pixels. Shape is preserved; pixel totals are sample
// counts. Exact ROI analysis still uses computeHistogram().
inline constexpr int kDisplayHistogramMaxEdge = 256;

inline Histogram computeDisplayHistogram(const ImageData &img, int bins = 256)
{
    if (img.isNull())
        return computeHistogram(img, bins);
    const int maxDim = std::max(img.width, img.height);
    if (maxDim <= kDisplayHistogramMaxEdge)
        return computeHistogram(img, bins);
    const int stepX =
        std::max(1, (img.width + kDisplayHistogramMaxEdge - 1) / kDisplayHistogramMaxEdge);
    const int stepY =
        std::max(1, (img.height + kDisplayHistogramMaxEdge - 1) / kDisplayHistogramMaxEdge);
    return computeHistogram(img, 0, 0, img.width, img.height, bins, stepX, stepY);
}

} // namespace mviewer::core
