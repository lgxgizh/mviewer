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

    // M23: luma channel (Rec.601) is computed alongside RGB.
    {
        std::vector<uint8_t> wp = {255, 255, 255};
        ImageData wimg = makeRgb(1, 1, wp);
        Histogram hw = computeHistogram(wimg);
        CHECK(hw.luma.size() == 256, "luma has 256 bins");
        CHECK(hw.luma[255] == 1, "white pixel -> luma 255");
        long ls = 0;
        for (long v : hw.luma)
            ls += v;
        CHECK(ls == hw.total, "luma sums to total");
    }

    // M23: ROI histogram — only pixels inside the ROI are counted.
    {
        // 4x1 image: two dark pixels then two bright pixels.
        const std::vector<uint8_t> rp = {0, 0, 0, 0, 0, 0, 200, 200, 200, 200, 200, 200};
        ImageData rimg = makeRgb(4, 1, rp);

        Histogram left = computeHistogram(rimg, 0, 0, 2, 1);
        CHECK(left.total == 2, "ROI left total = 2");
        CHECK(left.r[0] == 2 && left.r[200] == 0, "ROI left counts only dark pixels");

        Histogram right = computeHistogram(rimg, 2, 0, 2, 1);
        CHECK(right.total == 2 && right.r[200] == 2, "ROI right counts only bright pixels");

        // ROI partially outside the image is clipped.
        Histogram clip = computeHistogram(rimg, 3, 0, 10, 10);
        CHECK(clip.total == 1 && clip.r[200] == 1, "ROI clipped to bounds");

        // Degenerate and fully-outside ROIs yield empty histograms.
        Histogram deg = computeHistogram(rimg, 0, 0, 0, 0);
        CHECK(deg.total == 0, "degenerate ROI total = 0");
        Histogram off = computeHistogram(rimg, 10, 10, 2, 2);
        CHECK(off.total == 0, "outside ROI total = 0");

        // Full-rect ROI equals the whole-image histogram.
        Histogram full = computeHistogram(rimg, 0, 0, 4, 1);
        Histogram whole = computeHistogram(rimg);
        CHECK(full.total == whole.total && full.r == whole.r && full.luma == whole.luma,
              "full ROI == whole image");
    }

    printf("\nhistogram_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
