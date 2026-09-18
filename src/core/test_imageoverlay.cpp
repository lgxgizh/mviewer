// M22 unit tests: live analysis overlay math (zebra / false-color / channels).
#include "core/analysis/ImageOverlay.h"
#include "core/analysis/PixelGrid.h"
#include "core/image/ImageBuffer.h"

#include <cstdio>
#include <cstring>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,bugprone-easily-swappable-parameters)

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

static void testChannelIsolation()
{
    printf("\n[channel isolation]\n");
    fflush(stdout);
    ImageData red = makeImageData(8, 8, PixelFormat::RGB24);
    fillSolid(red, 200, 10, 20);
    mviewer::applyOverlay(red, mviewer::OverlayMode::ChannelR, 2);
    const uint8_t *pr = px(red.view(), 3, 3);
    CHECK(pr[0] == 200 && pr[1] == 200 && pr[2] == 200, "R plane becomes grayscale of red");

    ImageData green = makeImageData(8, 8, PixelFormat::RGB24);
    fillSolid(green, 200, 10, 20);
    mviewer::applyOverlay(green, mviewer::OverlayMode::ChannelG, 2);
    const uint8_t *pg = px(green.view(), 3, 3);
    CHECK(pg[0] == 10 && pg[1] == 10 && pg[2] == 10, "G plane becomes grayscale of green");

    ImageData blue = makeImageData(8, 8, PixelFormat::RGB24);
    fillSolid(blue, 200, 10, 20);
    mviewer::applyOverlay(blue, mviewer::OverlayMode::ChannelB, 2);
    const uint8_t *pb = px(blue.view(), 3, 3);
    CHECK(pb[0] == 20 && pb[1] == 20 && pb[2] == 20, "B plane becomes grayscale of blue");

    ImageData luma = makeImageData(8, 8, PixelFormat::RGB24);
    fillSolid(luma, 200, 10, 20);
    mviewer::applyOverlay(luma, mviewer::OverlayMode::ChannelY, 2);
    const uint8_t *py = px(luma.view(), 3, 3);
    const int expectedY = luminance(200, 10, 20);
    CHECK(py[0] == expectedY && py[1] == expectedY && py[2] == expectedY,
          "Y plane becomes BT.601 luminance grayscale");

    ImageData val = makeImageData(8, 8, PixelFormat::RGB24);
    fillSolid(val, 200, 10, 20);
    mviewer::applyOverlay(val, mviewer::OverlayMode::ChannelV, 2);
    const uint8_t *pv = px(val.view(), 3, 3);
    CHECK(pv[0] == 200 && pv[1] == 200 && pv[2] == 200,
          "V plane becomes HSV-V max(R,G,B) grayscale");
    CHECK(mviewer::isChannelOverlay(mviewer::OverlayMode::ChannelR), "R is a channel overlay");
    CHECK(mviewer::isChannelOverlay(mviewer::OverlayMode::ChannelV), "V is a channel overlay");
    CHECK(!mviewer::isChannelOverlay(mviewer::OverlayMode::Zebra),
          "zebra is not a channel overlay");
}

static void testPixelGrid()
{
    printf("\n[pixel grid]\n");
    fflush(stdout);
    CHECK(!mviewer::pixelGridVisible(1.0), "fit/100% does not show a pixel grid");
    CHECK(!mviewer::pixelGridVisible(7.99), "799% stays below the grid threshold");
    CHECK(mviewer::pixelGridVisible(8.0), "800% shows a pixel grid");

    const auto hidden = mviewer::enumeratePixelGrid(0, 0, 64, 64, 0, 0, 16, 16, 0, 0, 64, 64);
    CHECK(hidden.empty(), "scale 4x emits no grid lines");

    const auto lines = mviewer::enumeratePixelGrid(0, 0, 128, 128, 0, 0, 16, 16, 0, 0, 128, 128);
    CHECK(!lines.empty(), "scale 8x emits grid lines");
    int vertical = 0;
    int horizontal = 0;
    for (const auto &line : lines)
    {
        if (line.x1 == line.x2)
            ++vertical;
        if (line.y1 == line.y2)
            ++horizontal;
    }
    CHECK(vertical >= 16 && horizontal >= 16, "grid covers source columns and rows");

    const auto clipped = mviewer::enumeratePixelGrid(0, 0, 128, 128, 0, 0, 16, 16, 0, 0, 16, 16);
    CHECK(!clipped.empty() && clipped.size() < lines.size(),
          "visible clip reduces the emitted lattice");
}

static void testZebraGrayscale()
{
    printf("\n[zebra grayscale overlay]\n");
    fflush(stdout);
    ImageData white = makeImageData(16, 16, PixelFormat::Grayscale8);
    for (int i = 0; i < 16 * 16; ++i)
        white.buffer->data()[i] = 255;
    mviewer::applyOverlay(white, mviewer::OverlayMode::Zebra, 2);
    bool sawBlack = false, sawWhite = false;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            uint8_t val = white.buffer->data()[y * 16 + x];
            if (val == 0)
                sawBlack = true;
            if (val == 255)
                sawWhite = true;
        }
    CHECK(sawBlack, "zebra paints black stripes on over-exposed grayscale");
    CHECK(sawWhite, "zebra leaves non-stripe grayscale white");
}

static void testBGRAndAlphaPreservation()
{
    printf("\n[BGR and alpha preservation]\n");
    fflush(stdout);
    auto makeBGR = []()
    {
        ImageData img = makeImageData(8, 8, PixelFormat::BGR24);
        const ImageBuffer vb = img.view();
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                uint8_t *p =
                    vb.data + static_cast<size_t>(y) * vb.stride() + static_cast<size_t>(x) * 3;
                p[0] = 30; // B
                p[1] = 20; // G
                p[2] = 10; // R
            }
        return img;
    };

    ImageData bgrR = makeBGR();
    mviewer::applyOverlay(bgrR, mviewer::OverlayMode::ChannelR, 2);
    const uint8_t *pr = px(bgrR.view(), 0, 0);
    CHECK(pr[0] == 10 && pr[1] == 10 && pr[2] == 10, "BGR24 ChannelR extracts red correctly");

    ImageData bgrB = makeBGR();
    mviewer::applyOverlay(bgrB, mviewer::OverlayMode::ChannelB, 2);
    const uint8_t *pb = px(bgrB.view(), 0, 0);
    CHECK(pb[0] == 30 && pb[1] == 30 && pb[2] == 30, "BGR24 ChannelB extracts blue correctly");

    ImageData rgba = makeImageData(8, 8, PixelFormat::RGBA32);
    fillSolid(rgba, 100, 150, 200);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            rgba.buffer
                ->data()[static_cast<size_t>(y) * rgba.stride() + static_cast<size_t>(x) * 4 + 3] =
                177;
    mviewer::applyOverlay(rgba, mviewer::OverlayMode::FalseColor, 2);
    bool alphaIntact = true;
    for (int y = 0; y < 8 && alphaIntact; ++y)
        for (int x = 0; x < 8; ++x)
            if (rgba.buffer->data()[static_cast<size_t>(y) * rgba.stride() +
                                    static_cast<size_t>(x) * 4 + 3] != 177)
                alphaIntact = false;
    CHECK(alphaIntact, "FalseColor preserves alpha channel in RGBA32");
}

static void testSimdChannelParity()
{
    printf("\n[SIMD channel parity and unaligned dimensions]\n");
    fflush(stdout);

    const std::vector<std::pair<int, int>> dimensions = {
        {1, 1}, {13, 7}, {16, 16}, {37, 19}, {64, 32}};

    for (const auto &[w, h] : dimensions)
    {
        for (auto fmt :
             {PixelFormat::RGBA32, PixelFormat::BGRA32, PixelFormat::RGB24, PixelFormat::BGR24})
        {
            const bool isBgr = (fmt == PixelFormat::BGRA32 || fmt == PixelFormat::BGR24);
            const int cpp = (fmt == PixelFormat::RGBA32 || fmt == PixelFormat::BGRA32) ? 4 : 3;

            for (auto mode : {mviewer::OverlayMode::ChannelR, mviewer::OverlayMode::ChannelG,
                              mviewer::OverlayMode::ChannelB, mviewer::OverlayMode::ChannelV,
                              mviewer::OverlayMode::ChannelY})
            {
                ImageData img = makeImageData(w, h, fmt);
                std::vector<uint8_t> ref(static_cast<size_t>(w) * static_cast<size_t>(h) *
                                         static_cast<size_t>(cpp));

                // Populate with deterministic pseudo-random gradient
                for (size_t idx = 0; idx < ref.size(); ++idx)
                {
                    const uint8_t val = static_cast<uint8_t>((idx * 37 + 11) & 0xFF);
                    img.buffer->data()[idx] = val;
                    ref[idx] = val;
                }

                // Compute reference
                for (size_t p = 0; p < static_cast<size_t>(w) * static_cast<size_t>(h); ++p)
                {
                    const uint8_t b = isBgr ? ref[p * cpp + 0] : ref[p * cpp + 2];
                    const uint8_t g = ref[p * cpp + 1];
                    const uint8_t r = isBgr ? ref[p * cpp + 2] : ref[p * cpp + 0];
                    uint8_t expected = 0;
                    if (mode == mviewer::OverlayMode::ChannelR)
                        expected = r;
                    else if (mode == mviewer::OverlayMode::ChannelG)
                        expected = g;
                    else if (mode == mviewer::OverlayMode::ChannelB)
                        expected = b;
                    else if (mode == mviewer::OverlayMode::ChannelV)
                        expected = std::max({r, g, b});
                    else if (mode == mviewer::OverlayMode::ChannelY)
                        expected = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));

                    ref[p * cpp + 0] = expected;
                    ref[p * cpp + 1] = expected;
                    ref[p * cpp + 2] = expected;
                    // For cpp == 4, ref[p * 4 + 3] (alpha) remains unchanged
                }

                mviewer::applyOverlay(img, mode, 2);

                bool matches = true;
                bool alphaMatches = true;
                for (size_t idx = 0; idx < ref.size(); ++idx)
                {
                    if (img.buffer->data()[idx] != ref[idx])
                    {
                        matches = false;
                        if (cpp == 4 && (idx % 4) == 3)
                            alphaMatches = false;
                        break;
                    }
                }
                const char *fmtName = "Unknown";
                switch (fmt)
                {
                case PixelFormat::RGBA32:
                    fmtName = "RGBA32";
                    break;
                case PixelFormat::BGRA32:
                    fmtName = "BGRA32";
                    break;
                case PixelFormat::RGB24:
                    fmtName = "RGB24";
                    break;
                case PixelFormat::BGR24:
                    fmtName = "BGR24";
                    break;
                default:
                    break;
                }

                char desc[128];
                std::snprintf(desc, sizeof(desc), "%s %dx%d %s matches reference", fmtName, w, h,
                              mviewer::overlayModeLabel(mode));
                CHECK(matches, desc);
                if (cpp == 4)
                    CHECK(alphaMatches, "alpha channel remained intact");
            }
        }
    }
}

static void testNullAndBoundaryGuards()
{
    printf("\n[null and boundary guards]\n");
    fflush(stdout);

    ImageData nullImg{};
    mviewer::applyOverlay(nullImg, mviewer::OverlayMode::ChannelR, 2);
    mviewer::applyOverlay(nullImg, mviewer::OverlayMode::ChannelV, 2);
    mviewer::applyOverlay(nullImg, mviewer::OverlayMode::FalseColor, 2);
    mviewer::applyOverlay(nullImg, mviewer::OverlayMode::Zebra, 2);
    CHECK(nullImg.isNull(), "null image remains null after overlays");

    ImageData emptyImg = makeImageData(0, 0, PixelFormat::RGBA32);
    mviewer::applyOverlay(emptyImg, mviewer::OverlayMode::ChannelR, 2);
    mviewer::applyOverlay(emptyImg, mviewer::OverlayMode::ChannelV, 2);
    mviewer::applyOverlay(emptyImg, mviewer::OverlayMode::FalseColor, 2);
    mviewer::applyOverlay(emptyImg, mviewer::OverlayMode::Zebra, 2);
    CHECK(emptyImg.width == 0 && emptyImg.height == 0, "0x0 image handled gracefully");

    ImageData negImg = makeImageData(-1, -1, PixelFormat::RGB24);
    mviewer::applyOverlay(negImg, mviewer::OverlayMode::ChannelR, 2);
    mviewer::applyOverlay(negImg, mviewer::OverlayMode::ChannelV, 2);
    mviewer::applyOverlay(negImg, mviewer::OverlayMode::FalseColor, 2);
    mviewer::applyOverlay(negImg, mviewer::OverlayMode::Zebra, 2);
    CHECK(negImg.width < 0, "negative dimension image handled gracefully");
}

int main()
{
    printf("=== ImageOverlay Tests (M22) ===\n");
    fflush(stdout);
    testZebra();
    testZebraGrayscale();
    testFalseColor();
    testNoneNoOp();
    testChannelIsolation();
    testBGRAndAlphaPreservation();
    testPixelGrid();
    testSimdChannelParity();
    testNullAndBoundaryGuards();
    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg,bugprone-easily-swappable-parameters)
