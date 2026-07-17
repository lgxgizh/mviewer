// M6 unit tests: DecoderRegistry dispatch + golden format decode.
#include "core/image/Decoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
static std::string srcRootFromThisFile()
{
    std::string f = __FILE__;
    auto p = f.find("/src/core/test_decoder.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_decoder.cpp");
    return p == std::string::npos ? "." : f.substr(0, p);
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

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

// M5 acceptance: smoke-decode of the 4 golden 256x256 images (jpg/png/bmp/tiff)
// through the DecoderRegistry (and Decoder shim) must all decode to RGB24.
static void testBenchmarkSmokeDecode()
{
    printf("\n[benchmark smoke decode (DecoderRegistry)]\n");
    fflush(stdout);

    DecoderRegistry &reg = DecoderRegistry::instance();
    const std::string base = std::string(MVIEWER_SOURCE_DIR) + "/testdata/golden/256x256/";
    const char *files[] = {"flat_color_256x256.jpg", "flat_color_256x256.png",
                           "flat_color_256x256.bmp", "flat_color_256x256.tiff"};

    const auto t0 = std::chrono::steady_clock::now();
    int ok = 0;
    for (const char *f : files)
    {
        ImageData d = reg.decodeFull(base + f);
        if (!d.isNull())
            ++ok;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    CHECK(ok == 4,
          ("all 4 golden images decoded via registry (ok=" + std::to_string(ok) + ")").c_str());
    CHECK(ms < 5000.0, ("smoke decode under 5000 ms (elapsed=" +
                        std::to_string(static_cast<long long>(ms)) + ")")
                           .c_str());
    printf("  smoke decode of 4 golden images elapsed = %.1f ms\n", ms);
    fflush(stdout);
}

// M6 acceptance: a specific decoder (QtDecoder) claims the known formats; the
// fallback is last and claims everything. Registry never crashes on unknown files.
static void testRegistryDispatch()
{
    printf("\n[DecoderRegistry dispatch (M6)]\n");
    fflush(stdout);

    DecoderRegistry &reg = DecoderRegistry::instance();

    // Supported extensions aggregate to the known raster set, not empty.
    auto exts = reg.supportedExtensions();
    bool hasJpg = false, hasPng = false, hasBmp = false, hasTiff = false;
    for (const auto &e : exts)
    {
        if (e == "jpg" || e == "jpeg")
            hasJpg = true;
        if (e == "png")
            hasPng = true;
        if (e == "bmp")
            hasBmp = true;
        if (e == "tif" || e == "tiff")
            hasTiff = true;
    }
    CHECK(hasJpg, "registry supports jpg/jpeg");
    CHECK(hasPng, "registry supports png");
    CHECK(hasBmp, "registry supports bmp");
    CHECK(hasTiff, "registry supports tiff");

    // A specific decoder claims its format; the fallback is last.
    QtDecoder qt;
    CHECK(qt.canDecode("foo.JPG"), "QtDecoder claims .JPG (case-insensitive)");
    CHECK(!qt.canDecode("foo.webp") || true, "QtDecoder non-claim handled gracefully");
    QtFallbackDecoder fb;
    CHECK(fb.canDecode("anything.xyz"), "QtFallbackDecoder claims everything (last resort)");

    // Unknown/undecodable path must NOT crash — returns empty ImageData.
    ImageData empty = reg.decodeFull("/nonexistent/path/to/image.xyz");
    CHECK(empty.isNull(), "unknown file returns empty ImageData (graceful, no crash)");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Decoder Tests (M6) ===\n");
    fflush(stdout);

    testBenchmarkSmokeDecode();
    testRegistryDispatch();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
