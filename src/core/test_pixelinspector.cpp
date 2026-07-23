#include "core/analysis/PixelInspector.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace mviewer::core;

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

static void test_color_spaces()
{
    // Pure red: RGB(255,0,0).
    auto rgb = toColorSpace(255, 0, 0, ColorSpace::RGB);
    CHECK(std::abs(rgb.c1 - 255) < 1e-6 && std::abs(rgb.c2) < 1e-6 && std::abs(rgb.c3) < 1e-6);

    auto hsv = toColorSpace(255, 0, 0, ColorSpace::HSV);
    CHECK(std::abs(hsv.c1 - 0) < 1e-6);   // hue 0
    CHECK(std::abs(hsv.c3 - 100) < 1e-6); // value 100

    // White → Lab L≈100, a≈0, b≈0.
    auto lab = toColorSpace(255, 255, 255, ColorSpace::Lab);
    CHECK(std::abs(lab.c1 - 100) < 1e-3);
    CHECK(std::abs(lab.c2) < 1e-3);
    CHECK(std::abs(lab.c3) < 1e-3);

    // Black → Lab L≈0.
    auto labk = toColorSpace(0, 0, 0, ColorSpace::Lab);
    CHECK(std::abs(labk.c1) < 1e-3);

    // YUV: gray (128,128,128) → Y≈128, U≈0, V≈0.
    auto yuv = toColorSpace(128, 128, 128, ColorSpace::YUV);
    CHECK(std::abs(yuv.c1 - 128) < 1e-3);
    CHECK(std::abs(yuv.c2) < 1e-3);
    CHECK(std::abs(yuv.c3) < 1e-3);

    // YCbCr: gray (128,128,128) → Y≈128, Cb≈128, Cr≈128.
    auto yc = toColorSpace(128, 128, 128, ColorSpace::YCbCr);
    CHECK(std::abs(yc.c1 - 128) < 1e-3);
    CHECK(std::abs(yc.c2 - 128) < 1e-3);
    CHECK(std::abs(yc.c3 - 128) < 1e-3);
}

static void test_neighborhood()
{
    // 3×3 image, solid value 100 → every stat must equal 100/0.
    const int w = 3, h = 3;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 3, 100); // gray, lum=100
    auto s = neighborhoodStats(buf.data(), w * 3, w, h, 1, 1, 3);
    CHECK(s.count == 9);
    CHECK(std::abs(s.mean - 100) < 1e-6);
    CHECK(std::abs(s.variance) < 1e-6);
    CHECK(std::abs(s.stdDev) < 1e-6);
    CHECK(s.min == 100 && s.max == 100);

    // Gradient: increasing rows 0,10,20 → lum 0,10,20 at the three rows.
    std::vector<uint8_t> g(static_cast<size_t>(w) * h * 3, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t v = static_cast<uint8_t>(y * 10);
            size_t i = (static_cast<size_t>(y) * w + x) * 3;
            g[i] = g[i + 1] = g[i + 2] = v;
        }
    auto sg = neighborhoodStats(g.data(), w * 3, w, h, 1, 1, 3);
    // 3×3 around the center row samples {0,0,0, 10,10,10, 20,20,20} → mean 10.
    CHECK(std::abs(sg.mean - 10) < 1e-6);
    CHECK(sg.min == 0 && sg.max == 20);
    CHECK(sg.count == 9);
    // population variance = (3·0² + 3·10² + 3·20²)/9 − 10² = 66.67
    const double expectedVar = (3.0 * (0 - 10) * (0 - 10) + 3.0 * (20 - 10) * (20 - 10)) / 9.0;
    CHECK(std::abs(sg.variance - expectedVar) < 1e-6);

    // Out-of-bounds center must be clipped (1×1 at corner = single pixel).
    auto corner = neighborhoodStats(g.data(), w * 3, w, h, 0, 0, 1);
    CHECK(corner.count == 1);
    CHECK(corner.mean == 0);
}

int main()
{
    test_color_spaces();
    test_neighborhood();
    if (g_failures == 0)
    {
        std::printf("PASS: pixelinspector_tests (%d checks)\n", 0);
        return 0;
    }
    std::printf("FAILED: pixelinspector_tests (%d failures)\n", g_failures);
    return 1;
}
