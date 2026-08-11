// M7 ② Compare Engine — Pixel module (5th of Layout/Sync/ROI/Diff/Pixel).
// PixelController reads the pixel at a shared image-space point from every
// compared cell and computes delta vs a base cell. Domain-free; no display.
//
// Stage 1 (format-aware sampling): probes route through the shared
// samplePixel() sampler in ImageBuffer.h, which canonicalises every
// PixelFormat to RGBA and never reads out of bounds — including against a
// malformed/truncated buffer.
#include "core/compare/PixelController.h"
#include "core/image/ImageBuffer.h"

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
    } while (0)

static ImageData makeRGB(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        (*d.buffer)[i * 3 + 0] = r;
        (*d.buffer)[i * 3 + 1] = g;
        (*d.buffer)[i * 3 + 2] = b;
    }
    return d;
}

// 2x2 image whose byte stream is 0,1,2,... so every sample coordinate yields a
// hand-derivable, format-specific literal.
static ImageData makeSequential(int w, int h, PixelFormat fmt)
{
    ImageData d = makeImageData(w, h, fmt);
    for (size_t i = 0; i < d.buffer->size(); ++i)
        (*d.buffer)[i] = static_cast<uint8_t>(i);
    return d;
}

static void testSamplerCanonical()
{
    printf("\n[samplePixel canonical RGBA]\n");
    fflush(stdout);
    // 2x2 sequential bytes. Row stride: RGB24/BGR24=6, RGBA32/BGRA32=8, gray=2.
    {
        ImageData d = makeSequential(2, 2, PixelFormat::RGB24);
        PixelRGBA p = samplePixel(d, 0, 0);
        CHECK(p.valid && p.r == 0 && p.g == 1 && p.b == 2 && p.a == 255,
              "RGB24 (0,0) -> (0,1,2,255)");
        p = samplePixel(d, 1, 1);
        CHECK(p.valid && p.r == 9 && p.g == 10 && p.b == 11 && p.a == 255,
              "RGB24 (1,1) -> (9,10,11,255)");
    }
    {
        ImageData d = makeSequential(2, 2, PixelFormat::RGBA32);
        PixelRGBA p = samplePixel(d, 0, 0);
        CHECK(p.valid && p.r == 0 && p.g == 1 && p.b == 2 && p.a == 3, "RGBA32 (0,0) -> (0,1,2,3)");
        p = samplePixel(d, 1, 1);
        CHECK(p.valid && p.r == 12 && p.g == 13 && p.b == 14 && p.a == 15,
              "RGBA32 (1,1) -> (12,13,14,15)");
    }
    {
        ImageData d = makeSequential(2, 2, PixelFormat::BGR24);
        PixelRGBA p = samplePixel(d, 0, 0);
        CHECK(p.valid && p.r == 2 && p.g == 1 && p.b == 0 && p.a == 255,
              "BGR24 (0,0) -> (2,1,0,255)");
        p = samplePixel(d, 1, 1);
        CHECK(p.valid && p.r == 11 && p.g == 10 && p.b == 9 && p.a == 255,
              "BGR24 (1,1) -> (11,10,9,255)");
    }
    {
        ImageData d = makeSequential(2, 2, PixelFormat::BGRA32);
        PixelRGBA p = samplePixel(d, 0, 0);
        CHECK(p.valid && p.r == 2 && p.g == 1 && p.b == 0 && p.a == 3, "BGRA32 (0,0) -> (2,1,0,3)");
        p = samplePixel(d, 1, 1);
        CHECK(p.valid && p.r == 14 && p.g == 13 && p.b == 12 && p.a == 15,
              "BGRA32 (1,1) -> (14,13,12,15)");
    }
    {
        ImageData d = makeSequential(2, 2, PixelFormat::Grayscale8);
        PixelRGBA p = samplePixel(d, 0, 0);
        CHECK(p.valid && p.r == 0 && p.g == 0 && p.b == 0 && p.a == 255,
              "Grayscale8 (0,0) -> (0,0,0,255)");
        p = samplePixel(d, 1, 1);
        CHECK(p.valid && p.r == 3 && p.g == 3 && p.b == 3 && p.a == 255,
              "Grayscale8 (1,1) -> (3,3,3,255)");
    }
}

static void testSamplerAlpha()
{
    printf("\n[samplePixel alpha contract]\n");
    fflush(stdout);
    // Non-alpha formats always canonicalise to alpha 255.
    CHECK(samplePixel(makeSequential(2, 2, PixelFormat::RGB24), 0, 0).a == 255,
          "RGB24 -> alpha 255");
    CHECK(samplePixel(makeSequential(2, 2, PixelFormat::BGR24), 0, 0).a == 255,
          "BGR24 -> alpha 255");
    CHECK(samplePixel(makeSequential(2, 2, PixelFormat::Grayscale8), 0, 0).a == 255,
          "Grayscale8 -> alpha 255");
    // Alpha formats surface the stored alpha byte (3 from the sequential fill).
    CHECK(samplePixel(makeSequential(2, 2, PixelFormat::RGBA32), 0, 0).a == 3,
          "RGBA32 -> stored alpha");
    CHECK(samplePixel(makeSequential(2, 2, PixelFormat::BGRA32), 0, 0).a == 3,
          "BGRA32 -> stored alpha");
}

static void testSamplerBounds()
{
    printf("\n[samplePixel bounds / null]\n");
    fflush(stdout);
    const ImageData empty;
    CHECK(!samplePixel(empty, 0, 0).valid, "default (null) image -> invalid");

    ImageData noBuf = makeImageData(2, 2, PixelFormat::RGB24);
    noBuf.buffer = nullptr;
    CHECK(!samplePixel(noBuf, 0, 0).valid, "null buffer -> invalid");

    ImageData noBytes = makeImageData(2, 2, PixelFormat::RGB24);
    noBytes.buffer->clear();
    CHECK(!samplePixel(noBytes, 0, 0).valid, "empty buffer -> invalid");

    ImageData d = makeSequential(2, 2, PixelFormat::RGB24);
    CHECK(!samplePixel(d, -1, 0).valid, "negative x -> invalid");
    CHECK(!samplePixel(d, 0, -1).valid, "negative y -> invalid");
    CHECK(!samplePixel(d, 2, 0).valid, "x == width -> invalid");
    CHECK(!samplePixel(d, 0, 2).valid, "y == height -> invalid");
    CHECK(samplePixel(d, 0, 0).valid && samplePixel(d, 1, 1).valid, "in-bounds coords -> valid");
}

static void testSamplerTruncated()
{
    printf("\n[samplePixel malformed/truncated buffer]\n");
    fflush(stdout);
    // 2x2 RGB24 holds 12 bytes; cut the tail so the last pixel is unreadable.
    ImageData rgb = makeSequential(2, 2, PixelFormat::RGB24);
    rgb.buffer->resize(10);
    CHECK(samplePixel(rgb, 0, 0).valid, "truncated RGB keeps whole leading pixels");
    CHECK(!samplePixel(rgb, 1, 1).valid, "truncated RGB tail pixel -> invalid, no OOB read");

    // 2x2 RGBA32 holds 16 bytes; cut 2 bytes so the last pixel is unreadable.
    ImageData rgba = makeSequential(2, 2, PixelFormat::RGBA32);
    rgba.buffer->resize(14);
    CHECK(samplePixel(rgba, 0, 0).valid, "truncated RGBA keeps whole leading pixels");
    CHECK(!samplePixel(rgba, 1, 1).valid, "truncated RGBA tail pixel -> invalid, no OOB read");

    // 2x2 grayscale holds 4 bytes; cut 1 byte.
    ImageData gray = makeSequential(2, 2, PixelFormat::Grayscale8);
    gray.buffer->resize(3);
    CHECK(samplePixel(gray, 0, 0).valid, "truncated grayscale keeps whole leading pixels");
    CHECK(!samplePixel(gray, 1, 1).valid, "truncated grayscale tail pixel -> invalid, no OOB read");
}

static void testProbe()
{
    printf("\n[PixelController::probe]\n");
    fflush(stdout);
    PixelController pc;
    // Two 4x4 cells: cell0 solid red, cell1 solid blue.
    std::vector<ImageData> frames = {makeRGB(4, 4, 255, 0, 0), makeRGB(4, 4, 0, 0, 255)};
    auto samples = pc.probe(frames, 2, 2);
    CHECK(samples.size() == 2, "probe returns one sample per cell");
    CHECK(samples[0].valid && samples[0].r == 255 && samples[0].g == 0 && samples[0].b == 0,
          "cell0 reads solid red at (2,2)");
    CHECK(samples[1].valid && samples[1].r == 0 && samples[1].g == 0 && samples[1].b == 255,
          "cell1 reads solid blue at (2,2)");

    // Out-of-bounds -> invalid.
    auto oob = pc.probe(frames, 100, 100);
    CHECK(!oob[0].valid && !oob[1].valid, "out-of-bounds probe -> invalid samples");
}

static void testProbeFormats()
{
    printf("\n[PixelController::probe format-aware]\n");
    fflush(stdout);
    PixelController pc;
    // Cell0 BGRA, cell1 grayscale, cell2 RGBA — all 2x2 sequential bytes.
    std::vector<ImageData> frames = {makeSequential(2, 2, PixelFormat::BGRA32),
                                     makeSequential(2, 2, PixelFormat::Grayscale8),
                                     makeSequential(2, 2, PixelFormat::RGBA32)};
    auto samples = pc.probe(frames, 1, 1);
    CHECK(samples.size() == 3, "probe returns one sample per cell");
    CHECK(samples[0].valid && samples[0].r == 14 && samples[0].g == 13 && samples[0].b == 12,
          "BGRA32 cell canonicalised to RGB");
    CHECK(samples[1].valid && samples[1].r == 3 && samples[1].g == 3 && samples[1].b == 3,
          "Grayscale8 cell canonicalised to RGB");
    CHECK(samples[2].valid && samples[2].r == 12 && samples[2].g == 13 && samples[2].b == 14,
          "RGBA32 cell canonicalised to RGB");
    auto oob = pc.probe(frames, 5, 5);
    CHECK(!oob[0].valid && !oob[1].valid && !oob[2].valid,
          "out-of-bounds probe stays invalid per cell");
}

static void testProbeTruncated()
{
    printf("\n[PixelController::probe malformed buffer]\n");
    fflush(stdout);
    PixelController pc;
    ImageData d = makeSequential(2, 2, PixelFormat::RGB24);
    d.buffer->resize(10);
    std::vector<ImageData> frames = {d};
    auto samples = pc.probe(frames, 1, 1);
    CHECK(!samples[0].valid, "truncated frame probe -> invalid without OOB read");
}

static void testDelta()
{
    printf("\n[PixelController::deltaAgainst]\n");
    fflush(stdout);
    PixelController pc;
    std::vector<ImageData> frames = {makeRGB(4, 4, 10, 20, 30), makeRGB(4, 4, 40, 50, 60)};
    auto res = pc.inspect(frames, 1, 1, 0);
    CHECK(res.deltas.size() == 2, "delta vector sized per cell");
    // delta of base against itself = 0.
    CHECK(res.deltas[0].dr == 0 && res.deltas[0].dg == 0 && res.deltas[0].db == 0 &&
              res.deltas[0].dist == 0.0,
          "base cell delta vs itself is zero");
    // cell1 - cell0 = (30,30,30), dist = sqrt(3*900)=sqrt(2700)~51.96.
    CHECK(res.deltas[1].dr == 30 && res.deltas[1].dg == 30 && res.deltas[1].db == 30,
          "cell1 delta = (+30,+30,+30) vs base");
    CHECK(std::abs(res.deltas[1].dist - 51.96) < 0.1, "cell1 euclidean distance ~51.96");
}

int main()
{
    printf("=== CompareEngine Pixel module tests (M7 ②) ===\n");
    fflush(stdout);
    testSamplerCanonical();
    testSamplerAlpha();
    testSamplerBounds();
    testSamplerTruncated();
    testProbe();
    testProbeFormats();
    testProbeTruncated();
    testDelta();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
