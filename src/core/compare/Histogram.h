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
struct Histogram
{
    std::vector<long> r, g, b;
    int bins = 256;
    long total = 0;
};

// Compute an RGB histogram over every pixel of @p img.
// Handles RGB24 / BGR24 / RGBA32 / BGRA32 / Grayscale8 layouts.
// Returns an empty histogram (all zeros) when @p img is null.
inline Histogram computeHistogram(const ImageData &img, int bins = 256)
{
    Histogram h;
    h.bins = std::max(1, bins);
    h.r.assign(static_cast<size_t>(h.bins), 0);
    h.g.assign(static_cast<size_t>(h.bins), 0);
    h.b.assign(static_cast<size_t>(h.bins), 0);

    if (img.isNull())
        return h;

    const ImageBuffer v = img.view();
    const int w = v.width;
    const int ht = v.height;
    const int cpp = v.channelsPerPixel();
    const long stride = static_cast<long>(v.stride());
    const uint8_t *data = v.data;

    const bool gray = (v.format == PixelFormat::Grayscale8);
    const bool bgr = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);

    for (int y = 0; y < ht; ++y)
    {
        const uint8_t *row = data + static_cast<ptrdiff_t>(y) * stride;
        for (int x = 0; x < w; ++x)
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
        }
    }
    h.total = static_cast<long>(w) * ht;
    return h;
}

} // namespace mviewer::core
