// M22 unit tests: live analysis overlay math (zebra / false-color).
#include "core/analysis/ImageOverlay.h"
#include "core/image/ImageBuffer.h"

#include <cstdio>
#include <cstring>

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

static void fillSolid(ImageData &img, uint8_t r, uint8_t g, uint8_t b)
{
    const ImageBuffer v = img.view();
    for (int y = 0; y < v.height; ++y)
        for (int x = 0; x < v.width; ++x)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride() +
                         static_cast<size_t>(x) * v.channelsPerPixel();
            p[0] = r;
            p[1] = g;
            p[2] = b;
            if (v.channelsPerPixel() >= 4)
                p[3] = 255;
        }
}

static const uint8_t *px(const ImageBuffer &v, int x, int y)
{
    return v.data + static_cast<size_t>(y) * v.stride() +
           static_cast<size_t>(x) * v.channelsPerPixel();
}

static void testZebra()
{
    printf("\n[zebra overlay]\n");
    fflush(stdout);
    // All-white: luminance 255 >= hi(250) → striped black overlay.
    ImageData white = makeImageData(16, 16, PixelFormat::RGBA32);
    fillSolid(white, 255, 255, 255);
    mviewer::applyOverlay(white, mviewer::OverlayMode::Zebra, 2);
    const ImageBuffer vw = white.view();
    bool sawBlack = false, sawWhite = false;
    for (int y = 0; y < vw.height; ++y)
        for (int x = 0; x < vw.width; ++x)
        {
            const uint8_t *p = px(vw, x, y);
            if (p[0] == 0 && p[1] == 0 && p[2] == 0)
                sawBlack = true;
            if (p[0] == 255 && p[1] == 255 && p[2] == 255)
                sawWhite = true;
        }
    CHECK(sawBlack, "zebra paints black stripes on over-exposed white");
    CHECK(sawWhite, "zebra leaves non-stripe pixels white (readable)");

    // All-black: luminance 0 <= lo(5) → striped white overlay.
    ImageData black = makeImageData(16, 16, PixelFormat::RGBA32);
    fillSolid(black, 0, 0, 0);
    mviewer::applyOverlay(black, mviewer::OverlayMode::Zebra, 2);
    const ImageBuffer vb = black.view();
    bool sawWhite2 = false;
    for (int y = 0; y < vb.height; ++y)
        for (int x = 0; x < vb.width; ++x)
        {
            const uint8_t *p = px(vb, x, y);
            if (p[0] == 255 && p[1] == 255 && p[2] == 255)
                sawWhite2 = true;
        }
    CHECK(sawWhite2, "zebra paints white stripes on under-exposed black");
}

static void testFalseColor()
{
    printf("\n[false-color overlay]\n");
    fflush(stdout);
    ImageData gray = makeImageData(16, 16, PixelFormat::RGBA32);
    fillSolid(gray, 128, 128, 128);
    mviewer::applyOverlay(gray, mviewer::OverlayMode::FalseColor, 2);
    const ImageBuffer v = gray.view();
    // 128/255 ≈ 0.502 → jet(0.5) is greenish, not gray (128,128,128).
    const uint8_t *p0 = px(v, 1, 0);
    CHECK(!(p0[0] == 128 && p0[1] == 128 && p0[2] == 128),
          "false-color remaps mid-gray to a non-gray jet color");
    // Constant input maps deterministically to a uniform color.
    bool uniform = true;
    for (int y = 0; y < v.height && uniform; ++y)
        for (int x = 0; x < v.width; ++x)
        {
            const uint8_t *p = px(v, x, y);
            if (!(p[0] == p0[0] && p[1] == p0[1] && p[2] == p0[2]))
            {
                uniform = false;
                break;
            }
        }
    CHECK(uniform, "constant-input false-color is uniform across pixels");
}

static void testNoneNoOp()
{
    printf("\n[none overlay is no-op]\n");
    fflush(stdout);
    ImageData img = makeImageData(8, 8, PixelFormat::RGBA32);
    fillSolid(img, 10, 20, 30);
    mviewer::applyOverlay(img, mviewer::OverlayMode::None, 2);
    const ImageBuffer v = img.view();
    const uint8_t *p = px(v, 0, 0);
    CHECK(p[0] == 10 && p[1] == 20 && p[2] == 30, "None overlay leaves pixels unchanged");
}

int main()
{
    printf("=== ImageOverlay Tests (M22) ===\n");
    fflush(stdout);
    testZebra();
    testFalseColor();
    testNoneNoOp();
    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
