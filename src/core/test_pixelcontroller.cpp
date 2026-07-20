// M7 ② Compare Engine — Pixel module (5th of Layout/Sync/ROI/Diff/Pixel).
// PixelController reads the pixel at a shared image-space point from every
// compared cell and computes delta vs a base cell. Domain-free; no display.
#include "core/compare/PixelController.h"
#include "core/image/ImageBuffer.h"

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
    testProbe();
    testDelta();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
