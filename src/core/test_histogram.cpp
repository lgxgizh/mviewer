//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P0 #③: unit tests for the domain-free RGB histogram computation.
//
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using namespace mviewer::core;

namespace
{
ImageData makeRgb(int w, int h, const std::vector<uint8_t> &px)
{
    ImageData img = makeImageData(w, h, PixelFormat::RGB24);
    std::memcpy(img.buffer->data(), px.data(), px.size());
    return img;
}
} // namespace

int main()
{
    // 2x2 image with 4 distinct pixels.
    const std::vector<uint8_t> px = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    ImageData img = makeRgb(2, 2, px);
    Histogram h = computeHistogram(img);
    CHECK(h.total == 4, "histogram total equals pixel count");
    CHECK(h.r[10] == 1 && h.r[40] == 1 && h.r[70] == 1 && h.r[100] == 1, "R bins correct");
    CHECK(h.g[20] == 1, "G bin correct");
    CHECK(h.b[120] == 1, "B bin correct");
    long s = 0;
    for (long v : h.r)
        s += v;
    CHECK(s == 4, "R channel sums to total");
    s = 0;
    for (long v : h.g)
        s += v;
    CHECK(s == 4, "G channel sums to total");
    s = 0;
    for (long v : h.b)
        s += v;
    CHECK(s == 4, "B channel sums to total");

    // Null image -> empty histogram.
    ImageData null;
    Histogram hn = computeHistogram(null);
    CHECK(hn.total == 0, "null image total is 0");
    CHECK(hn.r.size() == 256, "histogram has 256 bins");

    // BGR pixel order must be honoured.
    std::vector<uint8_t> bp = {30, 20, 10};
    ImageData bimg = makeImageData(1, 1, PixelFormat::BGR24);
    std::memcpy(bimg.buffer->data(), bp.data(), 3);
    Histogram hb = computeHistogram(bimg);
    CHECK(hb.r[10] == 1 && hb.g[20] == 1 && hb.b[30] == 1, "BGR channel swap correct");

    printf("\nhistogram_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
