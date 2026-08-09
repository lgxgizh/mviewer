// M26 Phase 5 — PreviewPanel statistics moved off the UI thread.
//
// The panel's full-image luminance/RGB means previously iterated every pixel
// of the QPixmap on the GUI thread inside the load callback (multi-second
// stalls on 24 MP files). computePreviewStats() is the pure worker-side
// replacement over ImageData. These tests pin its correctness and verify it
// stays cheap enough for a 24 MP fixture (the UI-gap measurement lives in the
// M26 soak scenarios).

#include "core/image/ImageBuffer.h"
#include "core/image/ImageStats.h"

#include <chrono>
#include <cstdio>
#include <string>
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

namespace
{

// Fill a w x h image with a single RGB color.
void fill(ImageData &d, uint8_t r, uint8_t g, uint8_t b)
{
    const auto v = d.view();
    const int cpp = d.channelsPerPixel();
    for (int y = 0; y < d.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < d.width; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * cpp;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            if (cpp == 4)
                p[3] = 255;
        }
    }
}

void testSolidColor()
{
    printf("\n[solid-color mean statistics]\n");
    fflush(stdout);
    ImageData d = makeImageData(4, 4, PixelFormat::RGB24);
    fill(d, 10, 20, 30);
    const auto s = mviewer::core::computePreviewStats(d);
    CHECK(s.valid, "stats valid");
    CHECK(s.rMean == 10 && s.gMean == 20 && s.bMean == 30, "RGB means exact for solid color");
    CHECK(s.lumMean >= 17.99 && s.lumMean <= 18.01,
          "luminance mean = (int)(0.299*10 + 0.587*20 + 0.114*30) = 18.0");
}

void testFormatsAgree()
{
    printf("\n[RGBA/BGRA/BGR/Grayscale agree with RGB24]\n");
    fflush(stdout);
    ImageData rgb = makeImageData(8, 8, PixelFormat::RGB24);
    fill(rgb, 100, 150, 200);
    const auto ref = mviewer::core::computePreviewStats(rgb);

    ImageData rgba = makeImageData(8, 8, PixelFormat::RGBA32);
    fill(rgba, 100, 150, 200);
    const auto sRgba = mviewer::core::computePreviewStats(rgba);

    ImageData bgra = makeImageData(8, 8, PixelFormat::BGRA32);
    fill(bgra, 200, 150, 100); // B,G,R,A layout
    const auto sBgra = mviewer::core::computePreviewStats(bgra);

    ImageData bgr = makeImageData(8, 8, PixelFormat::BGR24);
    fill(bgr, 200, 150, 100); // B,G,R layout
    const auto sBgr = mviewer::core::computePreviewStats(bgr);

    CHECK(sRgba.rMean == ref.rMean && sRgba.gMean == ref.gMean && sRgba.bMean == ref.bMean,
          "RGBA32 matches RGB24");
    CHECK(sBgra.rMean == ref.rMean && sBgra.gMean == ref.gMean && sBgra.bMean == ref.bMean,
          "BGRA32 matches RGB24");
    CHECK(sBgr.rMean == ref.rMean && sBgr.gMean == ref.gMean && sBgr.bMean == ref.bMean,
          "BGR24 matches RGB24");

    ImageData gray = makeImageData(8, 8, PixelFormat::Grayscale8);
    fill(gray, 100, 0, 0);
    const auto sGray = mviewer::core::computePreviewStats(gray);
    CHECK(sGray.rMean == 100 && sGray.gMean == 100 && sGray.bMean == 100,
          "grayscale broadcasts the sample to all channels");

    ImageData null;
    CHECK(!mviewer::core::computePreviewStats(null).valid, "null image -> invalid stats");
}

void test24MpBudget()
{
    printf("\n[24 MP fixture: stats complete fast (worker-side budget)]\n");
    fflush(stdout);
    ImageData big = makeImageData(6000, 4000, PixelFormat::RGB24);
    fill(big, 80, 120, 160);
    const auto t0 = std::chrono::steady_clock::now();
    const auto s = mviewer::core::computePreviewStats(big);
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    CHECK(s.valid && s.rMean == 80, "24 MP stats correct");
    printf("  24MP stats elapsed = %.1f ms\n", ms);
    fflush(stdout);
    // Generous Release budget (this loop runs on any thread; the point is it
    // is a one-pass pixel loop, not a QPixmap conversion + UI repaint).
    CHECK(ms < 2000.0, "24 MP stats complete within the worker budget");
}

} // namespace

int main()
{
    printf("=== M26 PreviewStats (UI-latency) tests ===\n");
    fflush(stdout);

    testSolidColor();
    testFormatsAgree();
    test24MpBudget();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
