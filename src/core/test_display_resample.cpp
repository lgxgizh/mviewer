#include "core/render/DisplayResample.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{

using mviewer::core::DisplayRasterLevel;
using mviewer::core::DisplayResampleRequest;

int g_failures = 0;

void expect(bool ok, const char *name)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

ImageData solid(int w, int h, uint8_t r, uint8_t g, uint8_t b,
                PixelFormat format = PixelFormat::RGB24)
{
    ImageData image = makeImageData(w, h, format);
    uint8_t *dst = image.buffer->data();
    const int channels = image.channelsPerPixel();
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            uint8_t *px =
                dst + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) *
                          static_cast<size_t>(channels);
            if (format == PixelFormat::Grayscale8)
            {
                px[0] = r;
                continue;
            }
            if (format == PixelFormat::BGR24 || format == PixelFormat::BGRA32)
            {
                px[0] = b;
                px[1] = g;
                px[2] = r;
            }
            else
            {
                px[0] = r;
                px[1] = g;
                px[2] = b;
            }
            if (channels == 4)
                px[3] = 255;
        }
    }
    return image;
}

void putRgb(ImageData &image, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *px =
        image.buffer->data() +
        (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 3u;
    px[0] = r;
    px[1] = g;
    px[2] = b;
}

uint8_t redAt(const ImageData &image, int x, int y)
{
    const uint8_t *px =
        image.buffer->data() +
        (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 3u;
    return px[0];
}

DisplayResampleRequest fullRequest(const ImageData &image, int targetW, int targetH)
{
    DisplayResampleRequest request;
    request.source = {0, 0, image.width, image.height};
    request.targetWidth = targetW;
    request.targetHeight = targetH;
    return request;
}

double meanRed(const ImageData &image)
{
    double sum = 0.0;
    const int count = image.width * image.height;
    for (int i = 0; i < count; ++i)
        sum += image.buffer->data()[static_cast<size_t>(i) * 3u];
    return count > 0 ? sum / count : 0.0;
}

int peakColumn(const ImageData &image, int row)
{
    int best = 0;
    uint8_t peak = 0;
    for (int x = 0; x < image.width; ++x)
    {
        const uint8_t value = redAt(image, x, row);
        if (value > peak)
        {
            peak = value;
            best = x;
        }
    }
    return best;
}

int peakRow(const ImageData &image, int column)
{
    int best = 0;
    uint8_t peak = 0;
    for (int y = 0; y < image.height; ++y)
    {
        const uint8_t value = redAt(image, column, y);
        if (value > peak)
        {
            peak = value;
            best = y;
        }
    }
    return best;
}

void checkLineSurvives(int src, int dst, int threshold, const char *name)
{
    ImageData image = solid(src, 8, 0, 0, 0);
    const int line = src / 2;
    for (int y = 0; y < image.height; ++y)
        putRgb(image, line, y, 255, 255, 255);
    const ImageData out = mviewer::core::resampleDisplay(image, fullRequest(image, dst, 4));
    const bool sizeOk =
        !out.isNull() && out.width == dst && out.height == 4 && out.format == PixelFormat::RGB24;
    int peak = 0;
    if (sizeOk)
    {
        for (int x = 0; x < out.width; ++x)
            peak = std::max(peak, static_cast<int>(redAt(out, x, 1)));
    }
    const int column = sizeOk ? peakColumn(out, 1) : -1;
    const int expected =
        static_cast<int>(std::lround((line + 0.5) * dst / static_cast<double>(src) - 0.5));
    const bool placed = sizeOk && std::abs(column - expected) <= 1;
    expect(sizeOk && peak >= threshold && placed, name);
    if (!(sizeOk && peak >= threshold && placed))
        std::printf("  peak=%d column=%d expected=%d\n", peak, column, expected);
}

void checkEdge(bool vertical, bool atStart, const char *name)
{
    constexpr int kSrc = 16;
    constexpr int kDst = 4;
    ImageData image = solid(kSrc, kSrc, 0, 0, 0);
    if (vertical)
    {
        const int x = atStart ? 0 : kSrc - 1;
        for (int y = 0; y < kSrc; ++y)
            putRgb(image, x, y, 255, 255, 255);
    }
    else
    {
        const int y = atStart ? 0 : kSrc - 1;
        for (int x = 0; x < kSrc; ++x)
            putRgb(image, x, y, 255, 255, 255);
    }
    const ImageData out = mviewer::core::resampleDisplay(image, fullRequest(image, kDst, kDst));
    bool ok = !out.isNull() && out.width == kDst && out.height == kDst;
    if (ok && vertical)
        ok = peakColumn(out, kDst / 2) == (atStart ? 0 : kDst - 1);
    if (ok && !vertical)
        ok = peakRow(out, kDst / 2) == (atStart ? 0 : kDst - 1);
    expect(ok, name);
}

DisplayRasterLevel level(int target, int covered)
{
    DisplayRasterLevel raster;
    raster.targetWidth = target;
    raster.targetHeight = target;
    raster.coveredSourceWidth = covered;
    raster.coveredSourceHeight = covered;
    return raster;
}

} // namespace

int main()
{
    {
        ImageData image = solid(6, 4, 10, 20, 30);
        putRgb(image, 2, 1, 7, 8, 9);
        const ImageData out = mviewer::core::resampleDisplay(image, fullRequest(image, 6, 4));
        bool exact = !out.isNull() && out.width == 6 && out.height == 4;
        if (exact)
        {
            for (int y = 0; y < 4 && exact; ++y)
                for (int x = 0; x < 6 && exact; ++x)
                    exact =
                        redAt(out, x, y) == redAt(image, x, y) &&
                        out.buffer->data()[(static_cast<size_t>(y) * 6u + static_cast<size_t>(x)) *
                                               3u +
                                           1] ==
                            image.buffer->data()
                                [(static_cast<size_t>(y) * 6u + static_cast<size_t>(x)) * 3u + 1];
        }
        expect(exact, "identity at scale 1 is bit-exact");
    }

    for (int scale : {2, 3, 4})
    {
        ImageData image = solid(scale * 5, scale * 4, 128, 64, 32);
        const ImageData out = mviewer::core::resampleDisplay(image, fullRequest(image, 5, 4));
        bool exact = !out.isNull();
        if (exact)
        {
            for (int i = 0; i < out.width * out.height; ++i)
            {
                const uint8_t *px = out.buffer->data() + static_cast<size_t>(i) * 3u;
                exact = exact && px[0] == 128 && px[1] == 64 && px[2] == 32;
            }
        }
        expect(exact, scale == 2   ? "constant survives 2x"
                      : scale == 3 ? "constant survives 3x"
                                   : "constant survives 4x");
    }

    checkLineSurvives(8, 4, 80, "1px line survives 2x");
    checkLineSurvives(12, 4, 50, "1px line survives 3x");
    checkLineSurvives(16, 4, 40, "1px line survives 4x");

    {
        ImageData image = solid(32, 24, 0, 0, 0);
        for (int y = 0; y < image.height; ++y)
        {
            for (int x = 0; x < image.width; ++x)
            {
                const uint8_t value = static_cast<uint8_t>((x * 3 + y * 5) % 251);
                putRgb(image, x, y, value, value, value);
            }
        }
        const double sourceMean = meanRed(image);
        const ImageData out = mviewer::core::resampleDisplay(image, fullRequest(image, 8, 6));
        const double delta = out.isNull() ? 999.0 : std::abs(meanRed(out) - sourceMean);
        expect(!out.isNull() && delta <= 2.0, "mean preserved within 2 gray levels");
        if (out.isNull() || delta > 2.0)
            std::printf("  mean delta=%f\n", delta);
    }

    checkEdge(true, true, "left edge line stays on the left");
    checkEdge(true, false, "right edge line stays on the right");
    checkEdge(false, true, "top edge line stays on the top");
    checkEdge(false, false, "bottom edge line stays on the bottom");

    {
        ImageData image = solid(8, 4, 250, 0, 0);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                putRgb(image, x, y, 10, 10, 10);
        DisplayResampleRequest request;
        request.source = {0, 0, 4, 4};
        request.targetWidth = 2;
        request.targetHeight = 2;
        const ImageData out = mviewer::core::resampleDisplay(image, request);
        bool isolated = !out.isNull() && out.width == 2 && out.height == 2;
        if (isolated)
        {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    isolated = isolated && redAt(out, x, y) < 40;
        }
        expect(isolated, "sub-rect ignores pixels outside the window");
    }

    {
        ImageData gray = solid(4, 2, 180, 0, 0, PixelFormat::Grayscale8);
        const ImageData out = mviewer::core::resampleDisplay(gray, fullRequest(gray, 4, 2));
        const bool ok = !out.isNull() && redAt(out, 1, 1) == 180 && out.buffer->data()[1] == 180 &&
                        out.buffer->data()[2] == 180;
        expect(ok, "gray identity expands to RGB");

        ImageData bgr = solid(3, 2, 255, 0, 0, PixelFormat::BGR24);
        const ImageData red = mviewer::core::resampleDisplay(bgr, fullRequest(bgr, 3, 2));
        const bool redOk = !red.isNull() && redAt(red, 0, 0) == 255 && red.buffer->data()[1] == 0 &&
                           red.buffer->data()[2] == 0;
        expect(redOk, "BGR identity maps red to RGB");
    }

    {
        const DisplayRasterLevel shown = level(100, 200);
        const DisplayRasterLevel coarse = level(40, 200);
        const DisplayRasterLevel fine = level(180, 200);
        expect(mviewer::core::isBlurrierDisplayStandIn(shown, coarse),
               "coarser density is a blurrier stand-in");
        expect(!mviewer::core::isBlurrierDisplayStandIn({}, coarse),
               "nothing shown is not a downgrade");
        expect(mviewer::core::isBlurrierDisplayStandIn(shown, {}),
               "invalid incoming is blurrier than a shown raster");

        const DisplayRasterLevel desired = fine;
        const DisplayRasterLevel cached[3] = {coarse, shown, fine};
        const int keep = mviewer::core::selectDisplayStandIn(cached, 3, shown, desired);
        expect(keep == 2, "pending stand-in keeps the sharper cached level");
        const int none = mviewer::core::selectDisplayStandIn(cached, 1, shown, desired);
        expect(none < 0, "no downgrade while the sharper raster is still pending");

        const DisplayRasterLevel empty{};
        const int first = mviewer::core::selectDisplayStandIn(cached, 3, empty, desired);
        expect(first == 2, "empty shown accepts the sharpest cached level");

        const DisplayRasterLevel wide = level(100, 200);
        const DisplayRasterLevel exact = level(50, 100);
        const DisplayRasterLevel sameDensity[2] = {wide, exact};
        const int preferred = mviewer::core::selectDisplayStandIn(sameDensity, 2, empty, exact);
        expect(preferred == 1, "equal density prefers the desired size");

        const int same = mviewer::core::selectDisplayStandIn(&shown, 1, shown, desired);
        expect(same < 0, "same coverage is not swapped in again");

        DisplayRasterLevel elsewhere = level(180, 200);
        elsewhere.originX = 500;
        const DisplayRasterLevel wantedHere = level(80, 100);
        const DisplayRasterLevel covering = level(40, 400);
        const DisplayRasterLevel choices[2] = {elsewhere, covering};
        const int covered = mviewer::core::selectDisplayStandIn(choices, 2, empty, wantedHere);
        expect(covered == 1, "sharper stand-in outside the view is not swapped in");
    }

    std::printf("=== Display resample tests: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
