#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/image/ImageBuffer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL: %s @line %d\n", #cond, __LINE__);                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (std::abs((a) - (b)) > (eps))                                                           \
        {                                                                                          \
            std::printf("FAIL: %s (%f) != %s (%f) @line %d\n", #a, static_cast<double>(a), #b,     \
                        static_cast<double>(b), __LINE__);                                         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using namespace mviewer::core;

void test_psnr_identical()
{
    // Grayscale8
    {
        ImageData a = makeImageData(64, 64, PixelFormat::Grayscale8);
        std::memset(a.buffer->data(), 128, a.buffer->size());
        ImageData b = makeImageData(64, 64, PixelFormat::Grayscale8);
        std::memset(b.buffer->data(), 128, b.buffer->size());
        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }

    // RGB24
    {
        ImageData a = makeImageData(64, 64, PixelFormat::RGB24);
        std::memset(a.buffer->data(), 200, a.buffer->size());
        ImageData b = makeImageData(64, 64, PixelFormat::RGB24);
        std::memset(b.buffer->data(), 200, b.buffer->size());
        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }

    // BGR24
    {
        ImageData a = makeImageData(64, 64, PixelFormat::BGR24);
        std::memset(a.buffer->data(), 50, a.buffer->size());
        ImageData b = makeImageData(64, 64, PixelFormat::BGR24);
        std::memset(b.buffer->data(), 50, b.buffer->size());
        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }

    // RGBA32
    {
        ImageData a = makeImageData(64, 64, PixelFormat::RGBA32);
        std::memset(a.buffer->data(), 77, a.buffer->size());
        ImageData b = makeImageData(64, 64, PixelFormat::RGBA32);
        std::memset(b.buffer->data(), 77, b.buffer->size());
        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }

    // BGRA32
    {
        ImageData a = makeImageData(64, 64, PixelFormat::BGRA32);
        std::memset(a.buffer->data(), 99, a.buffer->size());
        ImageData b = makeImageData(64, 64, PixelFormat::BGRA32);
        std::memset(b.buffer->data(), 99, b.buffer->size());
        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }
}

void test_psnr_math()
{
    // Grayscale8: 10x10, 1 pixel delta 10 => MSE = 100 / 100 = 1.0 => PSNR = 10 * log10(65025)
    // = 48.1308036
    {
        ImageData a = makeImageData(10, 10, PixelFormat::Grayscale8);
        std::memset(a.buffer->data(), 50, a.buffer->size());
        ImageData b = makeImageData(10, 10, PixelFormat::Grayscale8);
        std::memset(b.buffer->data(), 50, b.buffer->size());
        b.buffer->data()[0] = 60;

        const double expected = 10.0 * std::log10(65025.0 / 1.0);
        CHECK_NEAR(AnalysisEngine::psnr(a, b), expected, 1e-4);
    }

    // RGB24: 10x10, 1 pixel delta 10 in R channel => MSE = 100 / 300 = 1/3 => PSNR = 10 *
    // log10(195075) = 52.90200
    {
        ImageData a = makeImageData(10, 10, PixelFormat::RGB24);
        std::memset(a.buffer->data(), 50, a.buffer->size());
        ImageData b = makeImageData(10, 10, PixelFormat::RGB24);
        std::memset(b.buffer->data(), 50, b.buffer->size());
        b.buffer->data()[0] = 60; // Delta in R channel

        const double expected = 10.0 * std::log10(65025.0 / (100.0 / 300.0));
        CHECK_NEAR(AnalysisEngine::psnr(a, b), expected, 1e-4);
    }

    // RGBA32: Alpha channel differences must NOT affect PSNR
    {
        ImageData a = makeImageData(10, 10, PixelFormat::RGBA32);
        std::memset(a.buffer->data(), 50, a.buffer->size());
        ImageData b = makeImageData(10, 10, PixelFormat::RGBA32);
        std::memset(b.buffer->data(), 50, b.buffer->size());
        b.buffer->data()[3] = 255; // Delta only in Alpha channel

        CHECK_NEAR(AnalysisEngine::psnr(a, b), 100.0, 1e-6);
    }
}

void test_psnr_cross_format()
{
    // RGB24 vs BGR24 with identical color content
    {
        ImageData rgb = makeImageData(16, 16, PixelFormat::RGB24);
        ImageData bgr = makeImageData(16, 16, PixelFormat::BGR24);
        for (int i = 0; i < 16 * 16; ++i)
        {
            const uint8_t r = static_cast<uint8_t>((i * 7) % 256);
            const uint8_t g = static_cast<uint8_t>((i * 13) % 256);
            const uint8_t b = static_cast<uint8_t>((i * 19) % 256);
            rgb.buffer->data()[i * 3 + 0] = r;
            rgb.buffer->data()[i * 3 + 1] = g;
            rgb.buffer->data()[i * 3 + 2] = b;

            bgr.buffer->data()[i * 3 + 0] = b;
            bgr.buffer->data()[i * 3 + 1] = g;
            bgr.buffer->data()[i * 3 + 2] = r;
        }
        CHECK_NEAR(AnalysisEngine::psnr(rgb, bgr), 100.0, 1e-6);
        CHECK_NEAR(AnalysisEngine::psnr(bgr, rgb), 100.0, 1e-6);
    }

    // RGBA32 vs BGRA32 with identical color content
    {
        ImageData rgba = makeImageData(16, 16, PixelFormat::RGBA32);
        ImageData bgra = makeImageData(16, 16, PixelFormat::BGRA32);
        for (int i = 0; i < 16 * 16; ++i)
        {
            const uint8_t r = static_cast<uint8_t>((i * 11) % 256);
            const uint8_t g = static_cast<uint8_t>((i * 17) % 256);
            const uint8_t b = static_cast<uint8_t>((i * 23) % 256);
            rgba.buffer->data()[i * 4 + 0] = r;
            rgba.buffer->data()[i * 4 + 1] = g;
            rgba.buffer->data()[i * 4 + 2] = b;
            rgba.buffer->data()[i * 4 + 3] = 255;

            bgra.buffer->data()[i * 4 + 0] = b;
            bgra.buffer->data()[i * 4 + 1] = g;
            bgra.buffer->data()[i * 4 + 2] = r;
            bgra.buffer->data()[i * 4 + 3] = 128; // Alpha ignored
        }
        CHECK_NEAR(AnalysisEngine::psnr(rgba, bgra), 100.0, 1e-6);
        CHECK_NEAR(AnalysisEngine::psnr(bgra, rgba), 100.0, 1e-6);
    }
}

void test_psnr_simd_large()
{
    // Test 320x240 image with random pattern to thoroughly exercise AVX2 / SSE2 lanes
    const int w = 320, h = 240;
    ImageData a = makeImageData(w, h, PixelFormat::RGB24);
    ImageData b = makeImageData(w, h, PixelFormat::RGB24);

    int64_t expectedSumSq = 0;
    const size_t totalBytes = static_cast<size_t>(w) * h * 3;
    for (size_t i = 0; i < totalBytes; ++i)
    {
        const uint8_t va = static_cast<uint8_t>((i * 37 + 11) % 256);
        const uint8_t vb = static_cast<uint8_t>((i * 53 + 7) % 256);
        a.buffer->data()[i] = va;
        b.buffer->data()[i] = vb;
        const int d = static_cast<int>(va) - static_cast<int>(vb);
        expectedSumSq += d * d;
    }

    const double expectedMse = static_cast<double>(expectedSumSq) / static_cast<double>(w * h * 3);
    const double expectedPsnr = 10.0 * std::log10(65025.0 / expectedMse);

    const double actualPsnr = AnalysisEngine::psnr(a, b);
    CHECK_NEAR(actualPsnr, expectedPsnr, 1e-4);
}

void test_ssim()
{
    // Identical images should yield SSIM = 1.0
    {
        ImageData a = makeImageData(32, 32, PixelFormat::Grayscale8);
        ImageData b = makeImageData(32, 32, PixelFormat::Grayscale8);
        for (size_t i = 0; i < a.buffer->size(); ++i)
        {
            a.buffer->data()[i] = static_cast<uint8_t>((i * 13) % 256);
            b.buffer->data()[i] = static_cast<uint8_t>((i * 13) % 256);
        }
        CHECK_NEAR(AnalysisEngine::ssim(a, b), 1.0, 1e-4);
    }

    // Slightly perturbed image should have SSIM in (0.7, 1.0)
    {
        ImageData a = makeImageData(32, 32, PixelFormat::Grayscale8);
        ImageData b = makeImageData(32, 32, PixelFormat::Grayscale8);
        for (size_t i = 0; i < a.buffer->size(); ++i)
        {
            a.buffer->data()[i] = static_cast<uint8_t>((i * 13) % 256);
            b.buffer->data()[i] = static_cast<uint8_t>((a.buffer->data()[i] + 5) % 256);
        }
        const double ssimVal = AnalysisEngine::ssim(a, b);
        CHECK(ssimVal > 0.7 && ssimVal < 1.0);
    }

    // Degenerate sizes (< 8x8) return 0.0
    {
        ImageData a = makeImageData(6, 6, PixelFormat::Grayscale8);
        ImageData b = makeImageData(6, 6, PixelFormat::Grayscale8);
        CHECK_NEAR(AnalysisEngine::ssim(a, b), 0.0, 1e-6);
    }
}

void test_noise_estimate()
{
    // Flat image has zero noise
    {
        ImageData flat = makeImageData(64, 64, PixelFormat::Grayscale8);
        std::memset(flat.buffer->data(), 120, flat.buffer->size());
        CHECK_NEAR(AnalysisEngine::noiseEstimate(flat), 0.0, 1e-6);
    }

    // Linear gradient has zero noise (Laplacian of linear is 0)
    {
        ImageData grad = makeImageData(64, 64, PixelFormat::Grayscale8);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                grad.buffer->data()[y * 64 + x] = static_cast<uint8_t>(x * 2 + y * 2);
        CHECK_NEAR(AnalysisEngine::noiseEstimate(grad), 0.0, 1e-4);
    }

    // High frequency checkerboard pattern has high noise variance
    {
        ImageData noisy = makeImageData(64, 64, PixelFormat::Grayscale8);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                noisy.buffer->data()[y * 64 + x] = ((x + y) % 2 == 0) ? 0 : 255;
        const double noise = AnalysisEngine::noiseEstimate(noisy);
        CHECK(noise > 1000.0);
    }
}

void test_stats_roi_bounds()
{
    ImageData img = makeImageData(100, 100, PixelFormat::RGB24);
    std::memset(img.buffer->data(), 128, img.buffer->size());

    // Normal ROI
    {
        mviewer::domain::Selection roi{10, 10, 20, 20};
        ImageStats s = AnalysisEngine::computeStatsROI(img, roi);
        CHECK(s.pixelCount == 400);
        CHECK_NEAR(s.lumMean, 128.0, 1.0);
    }

    // Negative / overflow ROI safely clamps
    {
        mviewer::domain::Selection roi{-50, -50, 60, 60};
        ImageStats s = AnalysisEngine::computeStatsROI(img, roi);
        CHECK(s.pixelCount == 100); // Clamped to [0,10)x[0,10) = 100 pixels
    }

    // Completely outside ROI
    {
        mviewer::domain::Selection roi{200, 200, 50, 50};
        ImageStats s = AnalysisEngine::computeStatsROI(img, roi);
        CHECK(s.pixelCount == 0);
    }
}

int main()
{
    std::printf("Running AnalysisEngine and PixelInspector test suite...\n");
    test_psnr_identical();
    test_psnr_math();
    test_psnr_cross_format();
    test_psnr_simd_large();
    test_ssim();
    test_noise_estimate();
    test_stats_roi_bounds();

    if (g_failures == 0)
    {
        std::printf("AnalysisEngine: ALL PASSED\n");
        return 0;
    }
    else
    {
        std::printf("AnalysisEngine: %d FAILS\n", g_failures);
        return 1;
    }
}
