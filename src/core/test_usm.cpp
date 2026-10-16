#include "core/image/ImageBuffer.h"
#include "core/image/UsmSharpen.h"

#include <cmath>
#include <cstring>
#include <iostream>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n";        \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << (msg) << "\n";                                                \
        }                                                                                          \
    } while (0)

using namespace mviewer::core;

static void testIdentity()
{
    ImageData src = makeImageData(32, 32, PixelFormat::RGB24);
    for (size_t i = 0; i < src.byteSize(); ++i)
        src.buffer->data()[i] = static_cast<uint8_t>((i * 17) & 0xFF);

    UsmParams pZeroAmount;
    pZeroAmount.amount = 0.0;
    pZeroAmount.radius = 1.5;
    ImageData out1 = applyUsmSharpen(src, pZeroAmount);
    CHECK(!out1.isNull(), "amount 0 returns valid image");
    CHECK(out1.width == src.width && out1.height == src.height, "dimensions match");
    CHECK(std::memcmp(out1.buffer->data(), src.buffer->data(), src.byteSize()) == 0,
          "amount 0 produces bit-identical output");

    UsmParams pZeroRadius;
    pZeroRadius.amount = 1.5;
    pZeroRadius.radius = 0.0;
    ImageData out2 = applyUsmSharpen(src, pZeroRadius);
    CHECK(std::memcmp(out2.buffer->data(), src.buffer->data(), src.byteSize()) == 0,
          "radius 0 produces bit-identical output");
}

static void testEdgeEnhancement()
{
    const int w = 64;
    const int h = 64;
    ImageData src = makeImageData(w, h, PixelFormat::Grayscale8);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            src.buffer->data()[y * w + x] = (x < w / 2) ? 80 : 180;
        }
    }

    UsmParams params;
    params.amount = 1.5;
    params.radius = 1.5;
    params.threshold = 4;
    params.lumaOnly = false;

    ImageData dst = applyUsmSharpen(src, params);
    CHECK(!dst.isNull(), "edge test sharpening succeeded");

    const int midY = h / 2;
    const uint8_t darkEdge = dst.buffer->data()[midY * w + (w / 2 - 1)];
    const uint8_t brightEdge = dst.buffer->data()[midY * w + (w / 2)];
    CHECK(darkEdge < 80, "dark side of edge undershoots to enhance contrast");
    CHECK(brightEdge > 180, "bright side of edge overshoots to enhance contrast");

    const uint8_t farDark = dst.buffer->data()[midY * w + 2];
    const uint8_t farBright = dst.buffer->data()[midY * w + (w - 3)];
    CHECK(farDark == 80, "flat dark area stays unchanged with threshold");
    CHECK(farBright == 180, "flat bright area stays unchanged with threshold");
}

static void testFormatCoverage()
{
    const PixelFormat formats[] = {PixelFormat::RGB24, PixelFormat::BGR24, PixelFormat::RGBA32,
                                   PixelFormat::BGRA32, PixelFormat::Grayscale8};
    UsmParams params;
    params.amount = 1.5;
    params.radius = 1.2;
    params.threshold = 2;

    for (PixelFormat fmt : formats)
    {
        ImageData img = makeImageData(16, 16, fmt);
        const int cpp = img.channelsPerPixel();
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                uint8_t *p = img.buffer->data() + (y * 16 + x) * cpp;
                for (int c = 0; c < cpp; ++c)
                    p[c] = static_cast<uint8_t>((x * 15 + y * 7 + c * 30) & 0xFF);
                if (cpp == 4)
                    p[3] = 210; // specific alpha
            }
        }

        ImageData res = applyUsmSharpen(img, params);
        CHECK(!res.isNull() && res.width == 16 && res.height == 16,
              "format produced valid sharpened output");
        CHECK(res.format == fmt, "output format matches source format");

        if (cpp == 4)
        {
            bool alphaPreserved = true;
            for (int y = 0; y < 16; ++y)
            {
                for (int x = 0; x < 16; ++x)
                {
                    if (res.buffer->data()[(y * 16 + x) * cpp + 3] != 210)
                        alphaPreserved = false;
                }
            }
            CHECK(alphaPreserved, "alpha channel is strictly preserved");
        }
    }
}

static void testLumaOnly()
{
    ImageData src = makeImageData(20, 20, PixelFormat::RGB24);
    for (int y = 0; y < 20; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            uint8_t *p = src.buffer->data() + (y * 20 + x) * 3;
            p[0] = (x < 10) ? 50 : 200;
            p[1] = (x < 10) ? 60 : 210;
            p[2] = (x < 10) ? 70 : 220;
        }
    }

    UsmParams params;
    params.amount = 1.5;
    params.radius = 1.0;
    params.threshold = 2;
    params.lumaOnly = true;

    ImageData dst = applyUsmSharpen(src, params);
    CHECK(!dst.isNull(), "luma-only sharpening succeeded");
    const uint8_t *darkP = dst.buffer->data() + (10 * 20 + 9) * 3;
    const uint8_t *brightP = dst.buffer->data() + (10 * 20 + 10) * 3;
    CHECK(darkP[0] < 50 && darkP[1] < 60 && darkP[2] < 70, "dark edge sharpened in luma-only");
    CHECK(brightP[0] > 200 && brightP[1] > 210 && brightP[2] > 220,
          "bright edge sharpened in luma-only");
}

static void testCancellation()
{
    ImageData src = makeImageData(200, 200, PixelFormat::RGB24);
    UsmParams params;
    params.amount = 1.5;
    params.radius = 5.0;

    int callCount = 0;
    auto cancelCb = [&callCount]()
    {
        ++callCount;
        return callCount > 1;
    };

    ImageData dst = applyUsmSharpen(src, params, cancelCb);
    CHECK(dst.isNull(), "cancelled sharpening returns empty image");
}

int main()
{
    std::cout << "=== USM Sharpening Core Tests ===\n";
    testIdentity();
    testEdgeEnhancement();
    testFormatCoverage();
    testLumaOnly();
    testCancellation();

    std::cout << "Results: " << (g_fail == 0 ? "ALL PASSED" : "FAILED") << "\n";
    return g_fail == 0 ? 0 : 1;
}
