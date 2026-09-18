// M13 — Analyzer extensions: MTF / Dead Pixel / ColorChecker.
// Headless correctness checks against synthetic images.
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/BlurAnalyzer.h"
#include "core/analyzer/BrightnessAnalyzer.h"
#include "core/analyzer/ColorCastAnalyzer.h"
#include "core/analyzer/ColorCheckerAnalyzer.h"
#include "core/analyzer/ContrastAnalyzer.h"
#include "core/analyzer/DeadPixelAnalyzer.h"
#include "core/analyzer/EntropyAnalyzer.h"
#include "core/analyzer/ExposureAnalyzer.h"
#include "core/analyzer/MTFAnalyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"
#include "core/analyzer/SharpnessAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <cmath>
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

ImageFrame makeFrame(int w, int h, const std::function<void(uint8_t *, int, int, int)> &fill,
                     PixelFormat fmt = PixelFormat::RGB24)
{
    mviewer::domain::ImageMetadata meta;
    meta.filePath = "synthetic";
    meta.width = w;
    meta.height = h;
    ImageData d = makeImageData(w, h, fmt);
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
    MTFAnalyzer b2;
    CHECK(b2.analyze(blur), "blurred edge analyzed");
    CHECK(mtf.mtf50() > 0.0 && mtf.mtf50() <= 1.0, "MTF50 in (0,1]");
    // A sharp step has higher spatial frequency content than a gradual ramp.
    CHECK(mtf.mtf50() > 0.25, "sharp edge yields a non-trivial MTF50");
    printf("    sharp MTF50=%.3f  blurred MTF50=%.3f\n", mtf.mtf50(), b2.mtf50());
    // The sharp step should resolve higher than the gentle ramp.
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

static void testRgbMean()
{
    printf("\n[RGBMean analyzer]\n");
    fflush(stdout);
    // Grayscale8 path (SSE2 accelerated)
    auto gray =
        makeFrame(64, 64, [](uint8_t *p, int, int, int) { p[0] = 120; }, PixelFormat::Grayscale8);
    RGBMeanAnalyzer aGray;
    CHECK(aGray.analyze(gray), "gray frame analyzed");
    CHECK(aGray.result().ok, "gray result ok");
    CHECK(std::abs(aGray.result().rMean - 120.0) < 1e-3, "gray rMean == 120");
    CHECK(std::abs(aGray.result().gMean - 120.0) < 1e-3, "gray gMean == 120");
    CHECK(std::abs(aGray.result().bMean - 120.0) < 1e-3, "gray bMean == 120");
    CHECK(aGray.result().rStd < 1e-3, "gray rStd ~ 0");

    // RGB24 path
    auto color = makeFrame(
        64, 64,
        [](uint8_t *p, int, int, int)
        {
            p[0] = 30;
            p[1] = 90;
            p[2] = 180;
        },
        PixelFormat::RGB24);
    RGBMeanAnalyzer aColor;
    CHECK(aColor.analyze(color), "color frame analyzed");
    CHECK(aColor.result().ok, "color result ok");
    CHECK(std::abs(aColor.result().rMean - 30.0) < 1e-3, "color rMean == 30");
    CHECK(std::abs(aColor.result().gMean - 90.0) < 1e-3, "color gMean == 90");
    CHECK(std::abs(aColor.result().bMean - 180.0) < 1e-3, "color bMean == 180");

    // Bounds & ROI validation
    mviewer::domain::Selection validRoi{10, 10, 20, 20};
    CHECK(aColor.analyzeRegion(color, validRoi), "valid ROI analyzed");
    mviewer::domain::Selection invertedRoi{20, 20, -10, -10};
    CHECK(!aColor.analyzeRegion(color, invertedRoi), "inverted ROI rejected");
    mviewer::domain::Selection zeroRoi{10, 10, 0, 0};
    CHECK(!aColor.analyzeRegion(color, zeroRoi), "zero ROI rejected");
}

static void testBrightnessContrast()
{
    printf("\n[Brightness & Contrast analyzers]\n");
    fflush(stdout);
    auto gray =
        makeFrame(64, 64, [](uint8_t *p, int, int, int) { p[0] = 150; }, PixelFormat::Grayscale8);
    BrightnessAnalyzer b;
    CHECK(b.analyze(gray), "brightness analyzed gray");
    CHECK(b.result().ok, "brightness ok");
    CHECK(std::abs(b.result().avgLum - 150.0) < 1e-3, "brightness avgLum == 150");
    CHECK(std::abs(b.result().minLum - 150.0) < 1e-3, "brightness minLum == 150");
    CHECK(std::abs(b.result().maxLum - 150.0) < 1e-3, "brightness maxLum == 150");

    ContrastAnalyzer c;
    CHECK(c.analyze(gray), "contrast analyzed gray");
    CHECK(c.result().ok, "contrast ok");
    CHECK(c.result().rms < 1e-3, "flat contrast rms ~ 0");

    // Inverted and out-of-bounds ROI rejection
    mviewer::domain::Selection invertedRoi{30, 30, -10, -10};
    mviewer::domain::Selection oobRoi{100, 100, 20, 20};
    CHECK(!b.analyzeRegion(gray, invertedRoi), "inverted ROI rejected by brightness");
    CHECK(!b.analyzeRegion(gray, oobRoi), "oob ROI rejected by brightness");
    CHECK(!c.analyzeRegion(gray, invertedRoi), "inverted ROI rejected by contrast");
    CHECK(!c.analyzeRegion(gray, oobRoi), "oob ROI rejected by contrast");
}

static void testBlurSharpnessNoise()
{
    printf("\n[Blur, Sharpness & Noise analyzers]\n");
    fflush(stdout);
    auto sharp = makeFrame(64, 64,
                           [](uint8_t *p, int, int y, int)
                           {
                               int v = y < 32 ? 20 : 220;
                               p[0] = p[1] = p[2] = static_cast<uint8_t>(v);
                           });
    auto smooth = makeFrame(64, 64, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 128; });
    auto noisy = makeFrame(64, 64,
                           [](uint8_t *p, int x, int y, int)
                           {
                               int v = 128 + (((x + y) % 2 == 0) ? 30 : -30);
                               p[0] = p[1] = p[2] = static_cast<uint8_t>(v);
                           });

    SharpnessAnalyzer s;
    CHECK(s.analyze(sharp), "sharpness analyzed sharp");
    double sharpVal = s.sharpnessValue();
    CHECK(s.analyze(smooth), "sharpness analyzed smooth");
    double smoothVal = s.sharpnessValue();
    CHECK(sharpVal > smoothVal, "sharpness: sharp > smooth");

    BlurAnalyzer bl;
    CHECK(bl.analyze(sharp), "blur analyzed sharp");
    double sharpVar = bl.result().variance;
    CHECK(bl.analyze(smooth), "blur analyzed smooth");
    double smoothVar = bl.result().variance;
    CHECK(sharpVar > smoothVar, "blur: sharp variance > smooth variance");

    NoiseAnalyzer nz;
    CHECK(nz.analyze(noisy), "noise analyzed noisy");
    double noisyLevel = nz.noiseLevel();
    CHECK(nz.analyze(smooth), "noise analyzed smooth");
    double cleanLevel = nz.noiseLevel();
    CHECK(noisyLevel > cleanLevel, "noise: noisy > smooth");

    // Inverted and degenerate ROI checks
    mviewer::domain::Selection invertedRoi{30, 30, -10, -10};
    mviewer::domain::Selection emptyRoi{10, 10, 0, 0};
    mviewer::domain::Selection smallRoi{0, 0, 2, 2};
    CHECK(!s.analyzeRegion(sharp, invertedRoi), "inverted ROI rejected by sharpness");
    CHECK(!s.analyzeRegion(sharp, emptyRoi), "empty ROI rejected by sharpness");
    CHECK(!bl.analyzeRegion(sharp, invertedRoi), "inverted ROI rejected by blur");
    CHECK(!bl.analyzeRegion(sharp, smallRoi), "small ROI rejected by blur (< 3x3)");
    CHECK(!nz.analyzeRegion(sharp, invertedRoi), "inverted ROI rejected by noise");
    CHECK(!nz.analyzeRegion(sharp, emptyRoi), "empty ROI rejected by noise");
}

static void testExposureColorCastEntropy()
{
    printf("\n[Exposure, ColorCast & Entropy analyzers]\n");
    fflush(stdout);
    auto dark = makeFrame(40, 40, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 10; });
    auto bright = makeFrame(40, 40, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 245; });
    auto red = makeFrame(40, 40,
                         [](uint8_t *p, int, int, int)
                         {
                             p[0] = 220;
                             p[1] = 30;
                             p[2] = 30;
                         });
    auto uniform = makeFrame(40, 40, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 128; });
    auto ramp = makeFrame(40, 40, [](uint8_t *p, int x, int, int w)
                          { p[0] = p[1] = p[2] = static_cast<uint8_t>((255 * x) / (w - 1)); });

    ExposureAnalyzer exp;
    CHECK(exp.analyze(dark), "exposure analyzed dark");
    CHECK(exp.result().shadowPct > 90.0, "dark frame shadowPct > 90%");
    CHECK(exp.analyze(bright), "exposure analyzed bright");
    CHECK(exp.result().highlightPct > 90.0, "bright frame highlightPct > 90%");

    ColorCastAnalyzer cast;
    CHECK(cast.analyze(red), "colorcast analyzed red");
    CHECK(cast.result().magnitude > 50.0, "red frame cast magnitude > 50");
    CHECK(cast.result().castR > 0.0, "red frame castR > 0");

    EntropyAnalyzer ent;
    CHECK(ent.analyze(uniform), "entropy analyzed uniform");
    CHECK(std::abs(ent.entropyValue()) < 1e-3, "uniform entropy is 0");
    CHECK(ent.analyze(ramp), "entropy analyzed ramp");
    CHECK(ent.entropyValue() > 2.0, "ramp entropy > 2 bits");

    // Inverted ROI checks
    mviewer::domain::Selection invertedRoi{30, 30, -10, -10};
    CHECK(!exp.analyzeRegion(dark, invertedRoi), "inverted ROI rejected by exposure");
    CHECK(!cast.analyzeRegion(red, invertedRoi), "inverted ROI rejected by colorcast");
    CHECK(!ent.analyzeRegion(ramp, invertedRoi), "inverted ROI rejected by entropy");
}

static void testCompareBounds()
{
    printf("\n[PSNR & SSIM analyzers bounds]\n");
    fflush(stdout);
    auto f1 = makeFrame(32, 32, [](uint8_t *p, int, int, int) { p[0] = p[1] = p[2] = 100; });
    PSNRAnalyzer psnr;
    psnr.setReference(f1);
    CHECK(psnr.analyze(f1), "psnr identical frame analyzed");
    CHECK(psnr.psnrValue() >= 99.0, "psnr identical frame >= 99 dB");

    SSIMAnalyzer ssim;
    ssim.setReference(f1);
    CHECK(ssim.analyze(f1), "ssim identical frame analyzed");
    CHECK(ssim.ssimValue() >= 0.99, "ssim identical frame >= 0.99");

    mviewer::domain::Selection invertedRoi{25, 25, -5, -5};
    mviewer::domain::Selection oobRoi{100, 100, 10, 10};
    CHECK(!psnr.analyzeRegion(f1, invertedRoi), "inverted ROI rejected by psnr");
    CHECK(!psnr.analyzeRegion(f1, oobRoi), "oob ROI rejected by psnr");
    CHECK(!ssim.analyzeRegion(f1, invertedRoi), "inverted ROI rejected by ssim");
    CHECK(!ssim.analyzeRegion(f1, oobRoi), "oob ROI rejected by ssim");
}

int main()
{
    printf("=== M13 analyzer extensions & optimizations ===\n");
    fflush(stdout);
    testMtf();
    testDeadPixel();
    testColorChecker();
    testRegistryHasNew();
    testRgbMean();
    testBrightnessContrast();
    testBlurSharpnessNoise();
    testExposureColorCastEntropy();
    testCompareBounds();
    printf("\n=== M13 analyzer extensions: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
