//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P0 #③: unit tests for the domain-free RGB histogram computation.
//
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "domain/Histogram.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,bugprone-implicit-widening-of-multiplication-result)

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
    int64_t s = 0;
    for (int64_t v : h.r)
        s += v;
    CHECK(s == 4, "R channel sums to total");
    s = 0;
    for (int64_t v : h.g)
        s += v;
    CHECK(s == 4, "G channel sums to total");
    s = 0;
    for (int64_t v : h.b)
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
        CHECK(hw.v.size() == 256, "HSV-V has 256 bins");
        CHECK(hw.v[255] == 1, "white pixel -> V 255");
        int64_t ls = 0;
        for (int64_t v : hw.luma)
            ls += v;
        CHECK(ls == hw.total, "luma sums to total");
        int64_t vs = 0;
        for (int64_t v : hw.v)
            vs += v;
        CHECK(vs == hw.total, "V channel sums to total");
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
        CHECK(full.total == whole.total && full.r == whole.r && full.luma == whole.luma &&
                  full.v == whole.v,
              "full ROI == whole image");
    }

    // Display histogram: small images stay exact; large images subsample.
    {
        Histogram exact = computeDisplayHistogram(img);
        CHECK(exact.total == 4 && exact.r[10] == 1,
              "display histogram is exact under the max edge");

        ImageData large = makeImageData(512, 512, PixelFormat::RGB24);
        std::memset(large.buffer->data(), 40, large.byteSize());
        Histogram sampled = computeDisplayHistogram(large);
        Histogram full = computeHistogram(large);
        CHECK(sampled.total > 0 && sampled.total < full.total,
              "display histogram samples fewer pixels than a full scan");
        CHECK(sampled.total <= (kDisplayHistogramMaxEdge + 1) * (kDisplayHistogramMaxEdge + 1),
              "display histogram stays near the 256-edge sample budget");
        CHECK(sampled.r[40] == sampled.total && sampled.g[40] == sampled.total &&
                  sampled.b[40] == sampled.total,
              "uniform large image keeps all display-histogram mass in one bin");
    }

    // Clipping analysis: shadow (0) and highlight (255) detection.
    {
        const std::vector<uint8_t> clipPx = {0, 0, 0, 255, 255, 255, 128, 128, 128, 64, 64, 64};
        ImageData clipImg = makeRgb(2, 2, clipPx);
        Histogram hClip = computeHistogram(clipImg);
        CHECK(hClip.luma[0] == 1, "shadow clipping at Y=0 correctly counted");
        CHECK(hClip.luma[255] == 1, "highlight clipping at Y=255 correctly counted");
    }

    // Grayscale8 image histogram
    {
        ImageData grayImg = makeImageData(4, 4, PixelFormat::Grayscale8);
        for (int i = 0; i < 16; ++i)
            grayImg.buffer->data()[i] = static_cast<uint8_t>(i * 10);
        Histogram hGray = computeHistogram(grayImg);
        CHECK(hGray.total == 16, "grayscale image total is 16");
        CHECK(hGray.r == hGray.g && hGray.r == hGray.b && hGray.r == hGray.luma &&
                  hGray.r == hGray.v,
              "grayscale channels all match luma and V");
    }

    // Pathological 64-bit ROI clamping test
    {
        ImageData img = makeImageData(32, 32, PixelFormat::Grayscale8);
        const int bigX = 0x7ffffff0;
        const int bigW = 100;
        Histogram hBig = computeHistogram(img, bigX, 0, bigW, 32);
        CHECK(hBig.total == 0, "pathological 64-bit ROI yields 0 total (no overflow)");
    }

    // Custom bin count
    {
        ImageData img = makeImageData(10, 10, PixelFormat::Grayscale8);
        std::memset(img.buffer->data(), 100, 100);
        Histogram h64 = computeHistogram(img, 64);
        CHECK(h64.bins == 64, "custom bins count is 64");
        CHECK(h64.total == 100, "custom bins total is 100");
        CHECK(h64.luma[25] == 100, "pixel value 100 maps to bin 25 in 64-bin histogram");
        CHECK(h64.luma[63] == 0, "bin 63 is empty when values are 100");

        // Edge values
        std::memset(img.buffer->data(), 0, 50);
        std::memset(img.buffer->data() + 50, 255, 50);
        Histogram hEdges = computeHistogram(img, 64);
        CHECK(hEdges.luma[0] == 50, "pixel value 0 maps to bin 0");
        CHECK(hEdges.luma[63] == 50, "pixel value 255 maps to bin 63");
    }

    // RGBA/BGRA: alpha is not a colour sample. Fully transparent pixels are
    // omitted; partial alpha is counted at its RGB value. V is max(R,G,B).
    {
        ImageData rgba = makeImageData(2, 1, PixelFormat::RGBA32);
        uint8_t *px = rgba.buffer ? rgba.buffer->data() : nullptr;
        CHECK(px != nullptr && rgba.buffer->size() >= 8, "rgba buffer allocated");
        if (px == nullptr || rgba.buffer->size() < 8)
            return 1;
        px[0] = 10;
        px[1] = 20;
        px[2] = 30;
        px[3] = 0;
        px[4] = 40;
        px[5] = 50;
        px[6] = 60;
        px[7] = 255;
        Histogram h = computeHistogram(rgba);
        CHECK(h.total == 1, "fully transparent RGBA pixel is omitted");
        CHECK(h.r[40] == 1 && h.r[10] == 0, "skipped pixel does not enter the red bins");
        CHECK(h.g[50] == 1 && h.b[60] == 1, "opaque RGBA pixel contributes RGB");
        CHECK(h.v[60] == 1, "V is max(R,G,B)");
        const int luma = luminance(40, 50, 60);
        CHECK(h.luma[static_cast<size_t>(luma)] == 1, "luma is Rec.601 of the opaque pixel");
        int64_t redSum = 0;
        for (int64_t bin : h.r)
            redSum += bin;
        CHECK(redSum == h.total, "alpha byte is not counted as an extra colour sample");

        Histogram hn = computeHistogram(rgba, 64);
        CHECK(hn.total == 1, "n-bin histogram also omits a fully transparent pixel");
        CHECK(hn.r[(40 * 64) / 256] == 1, "n-bin red uses the opaque pixel");
        CHECK(hn.v[(60 * 64) / 256] == 1, "n-bin V uses max(R,G,B)");

        px[3] = 128;
        Histogram partial = computeHistogram(rgba);
        CHECK(partial.total == 2, "partial alpha is counted at its RGB value");
        CHECK(partial.r[10] == 1 && partial.v[30] == 1, "partial pixel V is max of its RGB");

        ImageData bgra = makeImageData(1, 1, PixelFormat::BGRA32);
        uint8_t *bp = bgra.buffer ? bgra.buffer->data() : nullptr;
        CHECK(bp != nullptr && bgra.buffer->size() >= 4, "bgra buffer allocated");
        if (bp == nullptr || bgra.buffer->size() < 4)
            return 1;
        bp[0] = 10;
        bp[1] = 20;
        bp[2] = 30;
        bp[3] = 255;
        Histogram hb = computeHistogram(bgra);
        CHECK(hb.total == 1 && hb.r[30] == 1 && hb.g[20] == 1 && hb.b[10] == 1,
              "BGRA reads B,G,R and ignores the alpha byte");
        CHECK(hb.v[30] == 1, "BGRA V is max(R,G,B)");
        bp[3] = 0;
        CHECK(computeHistogram(bgra).total == 0, "fully transparent BGRA pixel is omitted");

        ImageFrame frame;
        px[3] = 0;
        frame.setPixels(rgba);
        frame.computeHistogram();
        const mviewer::domain::Histogram &dh = frame.histogram();
        CHECK(dh.v[60] == 1 && dh.red[10] == 0, "ImageFrame V counts only the opaque pixel");
        CHECK(dh.vMean == 60.0, "ImageFrame vMean is max(R,G,B) of counted pixels");
        CHECK(dh.totalPixels() == 1, "ImageFrame total ignores the transparent pixel");
        CHECK(dh.rMean == 40.0 && dh.gMean == 50.0 && dh.bMean == 60.0,
              "ImageFrame means divide by counted samples");

        ImageData clearPx = makeImageData(1, 1, PixelFormat::RGBA32);
        uint8_t *clearBytes = clearPx.buffer ? clearPx.buffer->data() : nullptr;
        CHECK(clearBytes != nullptr && clearPx.buffer->size() >= 4, "clear buffer allocated");
        if (clearBytes == nullptr || clearPx.buffer->size() < 4)
            return 1;
        clearBytes[0] = 9;
        clearBytes[3] = 0;
        ImageFrame clearFrame;
        clearFrame.setPixels(clearPx);
        clearFrame.computeHistogram();
        CHECK(clearFrame.histogram().vMean == 0.0 && clearFrame.histogram().totalPixels() == 0,
              "all-transparent ImageFrame keeps zero means");
    }

    // Bin counters are 64-bit so a single bin can pass 2^32 samples.
    {
        const int64_t huge = int64_t{1} << 40;
        Histogram wide;
        wide.bins = 256;
        wide.luma.assign(256, 0);
        wide.luma[0] = huge;
        wide.total = huge;
        CHECK(wide.luma[0] == huge, "core histogram bin holds 2^40");
        CHECK(wide.total == huge, "core histogram total holds 2^40");

        mviewer::domain::Histogram dh;
        dh.luminance[0] = huge;
        CHECK(dh.totalPixels() == huge, "domain histogram totalPixels is 64-bit");
    }

    printf("\nhistogram_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg,bugprone-implicit-widening-of-multiplication-result)
