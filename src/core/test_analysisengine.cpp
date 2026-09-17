//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// test_analysisengine.cpp — Comprehensive unit tests for AnalysisEngine
// Covers computeStatsROI, psnr, ssim, noiseEstimate, and format parity.
//

#include "core/analysis/AnalysisEngine.h"
#include "core/image/ImageBuffer.h"

#include <cmath>
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

namespace
{

ImageData makeColorImage(int w, int h, PixelFormat fmt, uint8_t r, uint8_t g, uint8_t b)
{
    ImageData img = makeImageData(w, h, fmt);
    const int cpp = img.channelsPerPixel();
    const size_t stride = img.stride();
    for (int y = 0; y < h; ++y)
    {
        uint8_t *row = img.buffer->data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * cpp;
            if (fmt == PixelFormat::Grayscale8)
            {
                p[0] = r;
            }
            else if (fmt == PixelFormat::BGR24 || fmt == PixelFormat::BGRA32)
            {
                p[0] = b;
                p[1] = g;
                p[2] = r;
                if (cpp == 4)
                    p[3] = 255;
            }
            else
            {
                p[0] = r;
                p[1] = g;
                p[2] = b;
                if (cpp == 4)
                    p[3] = 255;
            }
        }
    }
    return img;
}

void testStatsROI()
{
    printf("\n[AnalysisEngine::computeStatsROI]\n");

    // 1. Solid color RGB24
    ImageData imgRgb = makeColorImage(100, 100, PixelFormat::RGB24, 100, 150, 200);
    mviewer::domain::Selection full{0, 0, 100, 100};
    ImageStats sFull = AnalysisEngine::computeStatsROI(imgRgb, full);
    CHECK(sFull.pixelCount == 10000, "full RGB24 pixelCount == 10000");
    CHECK(sFull.rMean == 100 && sFull.gMean == 150 && sFull.bMean == 200, "RGB means exact");
    CHECK(sFull.vMean == 200.0, "V mean == 200");
    const int expLum = (19595 * 100 + 38470 * 150 + 7471 * 200) >> 16;
    CHECK(std::abs(sFull.lumMean - expLum) < 0.01, "lumMean exact");
    CHECK(sFull.histR[100] == 10000 && sFull.histG[150] == 10000 && sFull.histB[200] == 10000,
          "histogram bin counts exact");

    // 2. Format parity: BGR24, RGBA32, BGRA32, Grayscale8
    ImageData imgBgr = makeColorImage(100, 100, PixelFormat::BGR24, 100, 150, 200);
    ImageStats sBgr = AnalysisEngine::computeStatsROI(imgBgr, full);
    CHECK(sBgr.rMean == sFull.rMean && sBgr.gMean == sFull.gMean && sBgr.bMean == sFull.bMean,
          "BGR24 stats match RGB24");
    CHECK(sBgr.lumMean == sFull.lumMean && sBgr.vMean == sFull.vMean,
          "BGR24 lum/v mean match RGB24");

    ImageData imgRgba = makeColorImage(100, 100, PixelFormat::RGBA32, 100, 150, 200);
    ImageStats sRgba = AnalysisEngine::computeStatsROI(imgRgba, full);
    CHECK(sRgba.rMean == sFull.rMean && sRgba.gMean == sFull.gMean && sRgba.bMean == sFull.bMean,
          "RGBA32 stats match RGB24");

    ImageData imgBgra = makeColorImage(100, 100, PixelFormat::BGRA32, 100, 150, 200);
    ImageStats sBgra = AnalysisEngine::computeStatsROI(imgBgra, full);
    CHECK(sBgra.rMean == sFull.rMean && sBgra.gMean == sFull.gMean && sBgra.bMean == sFull.bMean,
          "BGRA32 stats match RGB24");

    ImageData imgGray = makeColorImage(100, 100, PixelFormat::Grayscale8, 128, 128, 128);
    ImageStats sGray = AnalysisEngine::computeStatsROI(imgGray, full);
    CHECK(sGray.pixelCount == 10000, "Grayscale8 pixelCount == 10000");
    CHECK(sGray.lumMean == 128.0 && sGray.vMean == 128.0 && sGray.rMean == 128.0,
          "Grayscale8 means exact");

    // 3. ROI bounds and negative coordinate clipping
    // Region starting at (-10, -10) with width 50, height 50.
    // Clamped visible region is [0, 40) x [0, 40) -> 40 x 40 = 1600 pixels.
    mviewer::domain::Selection negRoi{-10, -10, 50, 50};
    ImageStats sNeg = AnalysisEngine::computeStatsROI(imgRgb, negRoi);
    CHECK(sNeg.pixelCount == 1600, "negative ROI correctly clamped to 1600 pixels (not shifted)");
    CHECK(sNeg.rMean == 100, "negative ROI values intact");

    // Partial ROI overlapping bottom-right
    mviewer::domain::Selection brRoi{80, 80, 40, 40};
    ImageStats sBr = AnalysisEngine::computeStatsROI(imgRgb, brRoi);
    CHECK(sBr.pixelCount == 400, "bottom-right clipped ROI has 20x20 = 400 pixels");

    // Degenerate and outside ROIs
    mviewer::domain::Selection degenRoi{10, 10, 0, 0};
    CHECK(AnalysisEngine::computeStatsROI(imgRgb, degenRoi).pixelCount == 0,
          "degenerate ROI returns empty");

    mviewer::domain::Selection outRoi{200, 200, 50, 50};
    CHECK(AnalysisEngine::computeStatsROI(imgRgb, outRoi).pixelCount == 0,
          "outside ROI returns empty");

    ImageData nullImg;
    CHECK(AnalysisEngine::computeStatsROI(nullImg, full).pixelCount == 0,
          "null image returns empty");
}

void testPSNR()
{
    printf("\n[AnalysisEngine::psnr]\n");

    // 1. Identical images -> 100.0 dB
    ImageData imgA = makeColorImage(64, 64, PixelFormat::RGB24, 120, 130, 140);
    ImageData imgB = makeColorImage(64, 64, PixelFormat::RGB24, 120, 130, 140);
    CHECK(AnalysisEngine::psnr(imgA, imgB) == 100.0, "identical RGB24 psnr == 100.0");

    ImageData grayA = makeColorImage(64, 64, PixelFormat::Grayscale8, 80, 80, 80);
    ImageData grayB = makeColorImage(64, 64, PixelFormat::Grayscale8, 80, 80, 80);
    CHECK(AnalysisEngine::psnr(grayA, grayB) == 100.0, "identical Grayscale8 psnr == 100.0");

    ImageData rgbaA = makeColorImage(64, 64, PixelFormat::RGBA32, 10, 20, 30);
    ImageData rgbaB = makeColorImage(64, 64, PixelFormat::RGBA32, 10, 20, 30);
    CHECK(AnalysisEngine::psnr(rgbaA, rgbaB) == 100.0, "identical RGBA32 psnr == 100.0");

    // 2. Known uniform difference: diff = 10 on all pixels of RGB24
    // MSE = (10^2 + 10^2 + 10^2) / 3 = 100.
    // PSNR = 10 * log10(65025 / 100) = 10 * log10(650.25) ≈ 28.1308 dB
    ImageData imgDiff = makeColorImage(64, 64, PixelFormat::RGB24, 130, 140, 150);
    double p = AnalysisEngine::psnr(imgA, imgDiff);
    const double expPsnr = 10.0 * std::log10(65025.0 / 100.0);
    CHECK(std::abs(p - expPsnr) < 0.001, "RGB24 PSNR calculation accurate for MSE=100");

    // 3. Known uniform difference for Grayscale8: diff = 5
    // MSE = 25.
    // PSNR = 10 * log10(65025 / 25) = 10 * log10(2601) ≈ 34.1514 dB
    ImageData grayDiff = makeColorImage(64, 64, PixelFormat::Grayscale8, 85, 85, 85);
    double pGray = AnalysisEngine::psnr(grayA, grayDiff);
    const double expPGray = 10.0 * std::log10(65025.0 / 25.0);
    CHECK(std::abs(pGray - expPGray) < 0.001, "Grayscale8 PSNR calculation accurate for MSE=25");

    // 4. RGBA32 ignores alpha channel in PSNR
    ImageData rgbaDiff = makeColorImage(64, 64, PixelFormat::RGBA32, 20, 30, 40); // diff 10 on RGB
    double pRgba = AnalysisEngine::psnr(rgbaA, rgbaDiff);
    CHECK(std::abs(pRgba - expPsnr) < 0.001, "RGBA32 PSNR matches RGB24 for identical color delta");

    // 5. Null or empty inputs
    ImageData nullImg;
    CHECK(AnalysisEngine::psnr(nullImg, imgA) == 0.0, "null input yields psnr 0.0");
    ImageData emptyImg = makeImageData(0, 0, PixelFormat::RGB24);
    CHECK(AnalysisEngine::psnr(emptyImg, imgA) == 0.0, "empty input yields psnr 0.0");
}

void testSSIM()
{
    printf("\n[AnalysisEngine::ssim]\n");

    // 1. Identical images -> 1.0
    ImageData a = makeColorImage(32, 32, PixelFormat::Grayscale8, 128, 128, 128);
    ImageData b = makeColorImage(32, 32, PixelFormat::Grayscale8, 128, 128, 128);
    CHECK(std::abs(AnalysisEngine::ssim(a, b) - 1.0) < 0.0001, "identical Grayscale8 ssim == 1.0");

    ImageData aRgb = makeColorImage(32, 32, PixelFormat::RGB24, 100, 150, 200);
    ImageData bRgb = makeColorImage(32, 32, PixelFormat::RGB24, 100, 150, 200);
    CHECK(std::abs(AnalysisEngine::ssim(aRgb, bRgb) - 1.0) < 0.0001, "identical RGB24 ssim == 1.0");

    // 2. Distorted image -> ssim in [0, 1)
    ImageData c = makeColorImage(32, 32, PixelFormat::Grayscale8, 200, 200, 200);
    double s = AnalysisEngine::ssim(a, c);
    CHECK(s > 0.0 && s < 1.0, "distorted ssim in valid range (0, 1)");

    // 3. Small image (< 8x8) yields 0.0
    ImageData smallA = makeColorImage(6, 6, PixelFormat::Grayscale8, 100, 100, 100);
    ImageData smallB = makeColorImage(6, 6, PixelFormat::Grayscale8, 100, 100, 100);
    CHECK(AnalysisEngine::ssim(smallA, smallB) == 0.0, "sub-8x8 image yields ssim 0.0");
}

void testNoiseEstimate()
{
    printf("\n[AnalysisEngine::noiseEstimate]\n");

    ImageData flat = makeColorImage(50, 50, PixelFormat::Grayscale8, 128, 128, 128);
    CHECK(AnalysisEngine::noiseEstimate(flat) < 0.001, "flat image noise estimate ~ 0");

    ImageData flatRgb = makeColorImage(50, 50, PixelFormat::RGB24, 100, 150, 200);
    CHECK(AnalysisEngine::noiseEstimate(flatRgb) < 0.001, "flat RGB image noise estimate ~ 0");

    ImageData smallImg = makeColorImage(2, 2, PixelFormat::Grayscale8, 100, 100, 100);
    CHECK(AnalysisEngine::noiseEstimate(smallImg) == 0.0, "sub-3x3 image yields noise 0.0");
}

} // namespace

int main()
{
    testStatsROI();
    testPSNR();
    testSSIM();
    testNoiseEstimate();

    printf("\nAnalysisEngine tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
