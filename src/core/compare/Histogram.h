//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Histogram — domain-free RGB histogram computation for compare/inspection.
// No Qt dependency; operates directly on decoded ImageData.
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
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
    std::vector<long> v;    // HSV-V: max(R, G, B)
    int bins = 256;
    long total = 0;
};

namespace histogram_detail
{

inline Histogram makeEmpty(int bins)
{
    Histogram h;
    h.bins = std::max(1, bins);
    h.r.assign(static_cast<size_t>(h.bins), 0);
    h.g.assign(static_cast<size_t>(h.bins), 0);
    h.b.assign(static_cast<size_t>(h.bins), 0);
    h.luma.assign(static_cast<size_t>(h.bins), 0);
    h.v.assign(static_cast<size_t>(h.bins), 0);
    return h;
}

inline long accumulateGray256(const uint8_t *data, int64_t stride, int64_t x0, int64_t y0,
                              int64_t x1, int64_t y1, int sx, int sy, long *rAcc, long *gAcc,
                              long *bAcc, long *lumaAcc, long *vAcc)
{
    long samples = 0;
    for (int64_t y = y0; y < y1; y += sy)
    {
        const uint8_t *row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int64_t x = x0; x < x1; x += sx)
        {
            const uint8_t gVal = row[x];
            rAcc[gVal]++;
            gAcc[gVal]++;
            bAcc[gVal]++;
            lumaAcc[gVal]++;
            vAcc[gVal]++;
            ++samples;
        }
    }
    return samples;
}

inline long accumulateBgr256(const uint8_t *data, int64_t stride, int cpp, int64_t x0, int64_t y0,
                             int64_t x1, int64_t y1, int sx, int sy, long *rAcc, long *gAcc,
                             long *bAcc, long *lumaAcc, long *vAcc)
{
    long samples = 0;
    for (int64_t y = y0; y < y1; y += sy)
    {
        const uint8_t *row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int64_t x = x0; x < x1; x += sx)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * static_cast<size_t>(cpp);
            const uint8_t B = p[0];
            const uint8_t G = p[1];
            const uint8_t R = p[2];
            rAcc[R]++;
            gAcc[G]++;
            bAcc[B]++;
            const int Y = luminance(R, G, B);
            lumaAcc[Y]++;
            const uint8_t V = std::max({R, G, B});
            vAcc[V]++;
            ++samples;
        }
    }
    return samples;
}

inline long accumulateRgb256(const uint8_t *data, int64_t stride, int cpp, int64_t x0, int64_t y0,
                             int64_t x1, int64_t y1, int sx, int sy, long *rAcc, long *gAcc,
                             long *bAcc, long *lumaAcc, long *vAcc)
{
    long samples = 0;
    for (int64_t y = y0; y < y1; y += sy)
    {
        const uint8_t *row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int64_t x = x0; x < x1; x += sx)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * static_cast<size_t>(cpp);
            const uint8_t R = p[0];
            const uint8_t G = p[1];
            const uint8_t B = p[2];
            rAcc[R]++;
            gAcc[G]++;
            bAcc[B]++;
            const int Y = luminance(R, G, B);
            lumaAcc[Y]++;
            const uint8_t V = std::max({R, G, B});
            vAcc[V]++;
            ++samples;
        }
    }
    return samples;
}

inline Histogram accumulate256(const ImageBuffer &v, int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                               int sx, int sy, bool gray, bool bgr)
{
    Histogram h = makeEmpty(256);
    alignas(16) long rAcc[256]{};
    alignas(16) long gAcc[256]{};
    alignas(16) long bAcc[256]{};
    alignas(16) long lumaAcc[256]{};
    alignas(16) long vAcc[256]{};
    long samples = 0;
    const int cpp = v.channelsPerPixel();
    if (gray)
        samples = accumulateGray256(v.data, v.stride(), x0, y0, x1, y1, sx, sy, rAcc, gAcc, bAcc,
                                    lumaAcc, vAcc);
    else if (bgr)
        samples = accumulateBgr256(v.data, v.stride(), cpp, x0, y0, x1, y1, sx, sy, rAcc, gAcc,
                                   bAcc, lumaAcc, vAcc);
    else
        samples = accumulateRgb256(v.data, v.stride(), cpp, x0, y0, x1, y1, sx, sy, rAcc, gAcc,
                                   bAcc, lumaAcc, vAcc);
    std::memcpy(h.r.data(), rAcc, sizeof(rAcc));
    std::memcpy(h.g.data(), gAcc, sizeof(gAcc));
    std::memcpy(h.b.data(), bAcc, sizeof(bAcc));
    std::memcpy(h.luma.data(), lumaAcc, sizeof(lumaAcc));
    std::memcpy(h.v.data(), vAcc, sizeof(vAcc));
    h.total = samples;
    return h;
}

inline Histogram accumulateNBins(const ImageBuffer &v, int64_t x0, int64_t y0, int64_t x1,
                                 int64_t y1, int sx, int sy, bool gray, bool bgr, int bins)
{
    Histogram h = makeEmpty(bins);
    const int cpp = v.channelsPerPixel();
    const int maxBin = bins - 1;
    long samples = 0;
    for (int64_t y = y0; y < y1; y += sy)
    {
        const uint8_t *row = v.data + static_cast<size_t>(y) * static_cast<size_t>(v.stride());
        for (int64_t x = x0; x < x1; x += sx)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * static_cast<size_t>(cpp);
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
            h.r[std::min(R, maxBin)]++;
            h.g[std::min(G, maxBin)]++;
            h.b[std::min(B, maxBin)]++;
            const int Y = luminance(static_cast<uint8_t>(R), static_cast<uint8_t>(G),
                                    static_cast<uint8_t>(B));
            h.luma[std::min(Y, maxBin)]++;
            const int V = std::max({R, G, B});
            h.v[std::min(V, maxBin)]++;
            ++samples;
        }
    }
    h.total = samples;
    return h;
}

} // namespace histogram_detail

// M23 (ROI + Histogram 联动): compute an RGB+luma histogram over a rectangular
// region of @p img (image coordinates, clipped to bounds). A degenerate ROI
// (w/h <= 0 or fully outside) yields an all-zero histogram.
// Handles RGB24 / BGR24 / RGBA32 / BGRA32 / Grayscale8 layouts.
inline Histogram computeHistogram(const ImageData &img, int roiX, int roiY, int roiW, int roiH,
                                  int bins = 256, int stepX = 1, int stepY = 1)
{
    if (img.isNull() || roiW <= 0 || roiH <= 0)
        return histogram_detail::makeEmpty(bins);

    const ImageBuffer v = img.view();
    const int64_t x0 = std::clamp<int64_t>(roiX, 0, v.width);
    const int64_t y0 = std::clamp<int64_t>(roiY, 0, v.height);
    const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(roiX) + roiW, 0, v.width);
    const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(roiY) + roiH, 0, v.height);
    if (x0 >= x1 || y0 >= y1)
        return histogram_detail::makeEmpty(bins);

    const int sx = std::max(1, stepX);
    const int sy = std::max(1, stepY);
    const bool gray = (v.format == PixelFormat::Grayscale8);
    const bool bgr = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const int safeBins = std::max(1, bins);

    if (safeBins == 256)
        return histogram_detail::accumulate256(v, x0, y0, x1, y1, sx, sy, gray, bgr);
    return histogram_detail::accumulateNBins(v, x0, y0, x1, y1, sx, sy, gray, bgr, safeBins);
}

// Compute an RGB+luma histogram over every pixel of @p img.
// Returns an empty histogram (all zeros) when @p img is null.
inline Histogram computeHistogram(const ImageData &img, int bins = 256)
{
    if (img.isNull())
        return histogram_detail::makeEmpty(bins);
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
