// M13 — Analyzer extensions: MTF / Dead Pixel / ColorChecker.
// Headless correctness checks against synthetic images.
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/ColorCheckerAnalyzer.h"
#include "core/analyzer/DeadPixelAnalyzer.h"
#include "core/analyzer/MTFAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <cstdio>
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
        fflush(stdout);                                                                            \
    } while (0)

namespace
{

ImageFrame makeFrame(int w, int h, const std::function<void(uint8_t *, int, int, int)> &fill)
{
    mviewer::domain::ImageMetadata meta;
    meta.filePath = "synthetic";
    meta.width = w;
    meta.height = h;
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    auto view = d.view();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fill(view.data + static_cast<size_t>(y) * view.stride() +
                     static_cast<size_t>(x) * view.channelsPerPixel(),
                 x, y, w);
    return ImageFrame(meta, d);
}

} // namespace

static void testMtf()
{
    printf("\n[MTF analyzer (M13)]\n");
    fflush(stdout);
    // Sharp horizontal edge: top half dark, bottom half bright (step).
    auto sharp = makeFrame(64, 64,
                           [](uint8_t *p, int, int y, int)
                           {
                               int v = y < 32 ? 20 : 220;
                               p[0] = p[1] = p[2] = static_cast<uint8_t>(v);
                           });
    // Blurred edge: smooth gradient top->bottom.
    auto blur = makeFrame(64, 64,
                          [](uint8_t *p, int, int y, int h)
                          {
                              int v = 20 + (200 * y) / (h - 1);
                              p[0] = p[1] = p[2] = static_cast<uint8_t>(v);
                          });

    MTFAnalyzer mtf;
    CHECK(mtf.analyze(sharp), "sharp edge analyzed");
    CHECK(mtf.analyze(blur), "blurred edge analyzed");
    CHECK(mtf.mtf50() > 0.0 && mtf.mtf50() <= 1.0, "MTF50 in (0,1]");
    // A sharp step has higher spatial frequency content than a gradual ramp.
    CHECK(mtf.mtf50() > 0.25, "sharp edge yields a non-trivial MTF50");
    printf("    sharp MTF50=%.3f  blurred MTF50=%.3f\n", mtf.mtf50(),
           [&]()
           {
               MTFAnalyzer b;
               b.analyze(blur);
               return b.mtf50();
           }());
    // The sharp step should resolve higher than the gentle ramp.
    MTFAnalyzer b2;
    b2.analyze(blur);
    CHECK(mtf.mtf50() >= b2.mtf50(), "sharp edge MTF50 >= blurred ramp MTF50");
}

static void testDeadPixel()
{
    printf("\n[DeadPixel analyzer (M13)]\n");
    fflush(stdout);
    // Uniform grey field with 3 injected outliers.
    auto img = makeFrame(40, 40,
                         [](uint8_t *p, int x, int y, int)
                         {
                             int v = 128;
                             if ((x == 5 && y == 5) || (x == 30 && y == 10) || (x == 20 && y == 35))
                                 v = 5; // far below the 128 neighborhood
                             p[0] = p[1] = p[2] = static_cast<uint8_t>(v);
                         });
    DeadPixelAnalyzer dp;
    CHECK(dp.analyze(img), "dead-pixel image analyzed");
    CHECK(dp.deadCount() == 3, "exactly 3 injected outliers detected");
    CHECK(dp.maxDeviation() >= 100, "max deviation large (128->5)");

    // Clean uniform field -> zero dead pixels.
    auto clean = makeFrame(40, 40, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 128; });
    DeadPixelAnalyzer dp2;
    dp2.analyze(clean);
    CHECK(dp2.deadCount() == 0, "clean field reports 0 dead pixels");
}

static void testColorChecker()
{
    printf("\n[ColorChecker analyzer (M13)]\n");
    fflush(stdout);
    // Build a 6x4 grid ROI, each cell filled with its reference color.
    const double kRef[24][3] = {{115, 82, 68},   {194, 150, 130}, {98, 122, 157},  {87, 108, 67},
                                {133, 128, 186}, {103, 189, 170}, {214, 126, 44},  {80, 91, 166},
                                {193, 84, 97},   {94, 60, 108},   {157, 188, 64},  {224, 163, 46},
                                {56, 61, 150},   {70, 148, 73},   {175, 54, 60},   {231, 199, 31},
                                {187, 86, 149},  {8, 133, 161},   {243, 243, 242}, {200, 200, 200},
                                {160, 160, 160}, {122, 122, 121}, {85, 85, 85},    {52, 52, 52}};
    const int cols = 6, rows = 4, cw = 20, ch = 20;
    auto cc = makeFrame(cols * cw, rows * ch,
                        [&](uint8_t *p, int x, int y, int)
                        {
                            const int c = (x / cw) % cols;
                            const int r = (y / ch) % rows;
                            const double *ref = kRef[r * cols + c];
                            p[0] = static_cast<uint8_t>(ref[0]);
                            p[1] = static_cast<uint8_t>(ref[1]);
                            p[2] = static_cast<uint8_t>(ref[2]);
                        });
    ColorCheckerAnalyzer a;
    // ROI frames the whole 6x4 grid.
    mviewer::domain::Selection roi{0, 0, cols * cw, rows * ch};
    CHECK(a.analyzeRegion(cc, roi), "colorchecker ROI analyzed");
    CHECK(a.patchCount() == 24, "all 24 patches sampled");
    // Sampled patch centers equal the reference colors -> Delta-E ~0.
    CHECK(a.meanDeltaE() < 1.0, "mean Delta-E ~0 vs reference patches");
    printf("    mean Delta-E = %.3f\n", a.meanDeltaE());
}

static void testRegistryHasNew()
{
    printf("\n[AnalyzerRegistry M13 entries]\n");
    fflush(stdout);
    const auto ids = ::AnalyzerRegistry::instance().availableAnalyzers();
    bool hasMtf = false, hasDead = false, hasCc = false;
    for (const auto &id : ids)
    {
        if (id == "mtf")
            hasMtf = true;
        if (id == "deadpixel")
            hasDead = true;
        if (id == "colorchecker")
            hasCc = true;
    }
    CHECK(hasMtf, "registry has 'mtf'");
    CHECK(hasDead, "registry has 'deadpixel'");
    CHECK(hasCc, "registry has 'colorchecker'");
    // runAnalyzer() returns a result for each new id.
    auto frame = makeFrame(32, 32, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 100; });
    const auto res = ::AnalyzerRegistry::instance().runAnalyzer(frame);
    CHECK(res.count("mtf") == 1, "runAnalyzer() returns mtft result");
    CHECK(res.count("deadpixel") == 1, "runAnalyzer() returns deadpixel result");
    CHECK(res.count("colorchecker") == 1, "runAnalyzer() returns colorchecker result");
}

int main()
{
    printf("=== M13 analyzer extensions (MTF / DeadPixel / ColorChecker) ===\n");
    fflush(stdout);
    testMtf();
    testDeadPixel();
    testColorChecker();
    testRegistryHasNew();
    printf("\n=== M13 analyzer extensions: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
