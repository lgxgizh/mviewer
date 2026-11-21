// Review display adjustment (report feature 5): exposure, gamma, auto-stretch
// and 3D LUT application, plus the .cube parser.
#include "core/image/DisplayAdjust.h"

#include "core/filesystem/Utf8Path.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using mviewer::core::applyDisplayAdjust;
using mviewer::core::DisplayAdjust;

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

std::string writeCube(const fs::path &path, const std::string &body)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return "cannot open";
    out << body;
    out.close();
    return {};
}
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer_display_adjust_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    CHECK(!ec, "temp dir created");

    // A 2x2 gradient: R rises with x, G rises with y, B rises diagonally.
    ImageData src = makeImageData(2, 2, PixelFormat::RGB24);
    const std::uint8_t pixels[12] = {10, 20, 30, 200, 20, 40, 10, 210, 50, 200, 210, 60};
    std::memcpy(src.mutableView().data, pixels, sizeof(pixels));

    DisplayAdjust identityAdjust;
    CHECK(identityAdjust.identity(), "default adjustment is an identity");
    CHECK(applyDisplayAdjust(src, identityAdjust).buffer == src.buffer,
          "an identity adjustment shares the source buffer (no copy)");

    DisplayAdjust exposure;
    exposure.exposureEv = 1.0;
    CHECK(!exposure.identity() && exposure.fingerprint() != identityAdjust.fingerprint(),
          "exposure changes the fingerprint");
    const ImageData brighter = applyDisplayAdjust(src, exposure);
    CHECK(brighter.buffer != src.buffer, "a real adjustment produces a new buffer");
    const PixelRGBA p0 = samplePixel(src, 0, 0);
    const PixelRGBA p1 = samplePixel(brighter, 0, 0);
    CHECK(p1.r > p0.r && p1.g > p0.g, "+1 EV brightens every channel");

    DisplayAdjust gammaAdjust;
    gammaAdjust.gamma = 2.0; // brighten mid-tones
    const ImageData lifted = applyDisplayAdjust(src, gammaAdjust);
    const PixelRGBA g1 = samplePixel(lifted, 0, 0);
    CHECK(g1.r > p0.r && g1.r < 255, "gamma > 1 lifts mid-tones without clipping");

    DisplayAdjust stretch;
    stretch.autoStretch = true;
    const ImageData stretched = applyDisplayAdjust(src, stretch);
    const PixelRGBA lo = samplePixel(stretched, 0, 0); // min R 10, min G 20, min B 30
    CHECK(lo.r == 0 && lo.g == 0 && lo.b == 0, "auto-stretch maps the source minimum to 0");
    const PixelRGBA hi = samplePixel(stretched, 1, 1); // max R 200, max G 210, max B 60
    CHECK(hi.r == 255 && hi.g == 255 && hi.b == 255, "auto-stretch maps the source maximum to 255");

    // ── .cube parsing and application ──────────────────────────────────────
    const fs::path cubePath = root / "swap.cube";
    std::string cube = "TITLE \"swap\"\nLUT_3D_SIZE 2\n";
    // Adobe .cube order: blue varies fastest, then green, then red (the
    // canonical 2^3 example walks r, then g, then b). This LUT swaps R and B.
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r)
            {
                const double out[3] = {b * 1.0, g * 1.0, r * 1.0};
                cube += std::to_string(out[0]) + " " + std::to_string(out[1]) + " " +
                        std::to_string(out[2]) + "\n";
            }
    CHECK(writeCube(cubePath, cube).empty(), "cube fixture written");
    CHECK(DisplayAdjust::isCubePath(cubePath.string()), "a .cube path is recognized");

    DisplayAdjust lut;
    std::string error;
    CHECK(DisplayAdjust::loadCube(cubePath.string(), lut, error) && lut.lutSize == 2,
          "the .cube parses into a 2^3 LUT");
    CHECK(!lut.identity(), "a loaded LUT makes the adjustment non-identity");
    const ImageData swapped = applyDisplayAdjust(src, lut);
    const PixelRGBA s0 = samplePixel(swapped, 0, 0);
    CHECK(s0.r == 30 && s0.g == 20 && s0.b == 10, "the 3D LUT swaps R and B on a mid-tone pixel");

    // A corner pixel exercises the LUT's vertex directly: (255,255,0) -> (0,255,255).
    ImageData corner = makeImageData(1, 1, PixelFormat::RGB24);
    const std::uint8_t cornerPixels[3] = {255, 255, 0};
    std::memcpy(corner.mutableView().data, cornerPixels, sizeof(cornerPixels));
    const PixelRGBA swappedCorner = samplePixel(applyDisplayAdjust(corner, lut), 0, 0);
    CHECK(swappedCorner.r == 0 && swappedCorner.g == 255 && swappedCorner.b == 255,
          "the 3D LUT swaps R and B");

    const fs::path badCube = root / "bad.cube";
    CHECK(writeCube(badCube, "LUT_3D_SIZE 2\n0 0 0\n").empty(), "truncated cube written");
    DisplayAdjust rejected;
    CHECK(!DisplayAdjust::loadCube(badCube.string(), rejected, error) && !error.empty(),
          "a value count mismatch is rejected with a reason");

    // A Grayscale8 buffer is one byte per pixel. Auto-stretch used to index it
    // as RGB24 and read up to 3x past the end.
    ImageData gray = makeImageData(4, 3, PixelFormat::Grayscale8);
    for (int i = 0; i < 4 * 3; ++i)
        gray.mutableView().data[i] = static_cast<std::uint8_t>(i * 20);
    DisplayAdjust grayStretch;
    grayStretch.autoStretch = true;
    const ImageData grayOut = applyDisplayAdjust(gray, grayStretch);
    CHECK(grayOut.format == PixelFormat::Grayscale8, "grayscale auto-stretch stays grayscale");
    CHECK(grayOut.constView().data[0] == 0, "grayscale auto-stretch maps the minimum to 0");
    CHECK(grayOut.constView().data[4 * 3 - 1] == 255,
          "grayscale auto-stretch maps the maximum to 255");

    fs::remove_all(root, ec);
    printf("\n==== display adjust test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
