// M60: linked ROI geometry and source-domain RGB measurement gates.
//
// These tests deliberately stay below the UI boundary.  They freeze the
// canonical half-open rectangle contract and the format-aware statistics used
// by CompareWorkspace's asynchronous measurement path.

#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/image/ImageStats.h"
#include "core/image/ImageFrame.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>

namespace
{
int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void checkNear(double actual, double expected, const char *message)
{
    if (std::abs(actual - expected) > 1e-9)
    {
        std::printf("FAIL: %s (actual %.12f expected %.12f)\n", message, actual, expected);
        ++g_failures;
    }
}

struct RGB
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

ImageData makePattern(PixelFormat format)
{
    const auto data = makeImageData(2, 2, format);
    const RGB pattern[] = {{10, 20, 30}, {30, 40, 50}, {50, 60, 70}, {70, 80, 90}};
    const int cpp = data.channelsPerPixel();
    for (int i = 0; i < 4; ++i)
    {
        uint8_t *p = data.buffer->data() + static_cast<size_t>(i * cpp);
        const auto &v = pattern[i];
        if (format == PixelFormat::Grayscale8)
        {
            p[0] = v.r;
        }
        else if (format == PixelFormat::BGR24 || format == PixelFormat::BGRA32)
        {
            p[0] = v.b;
            p[1] = v.g;
            p[2] = v.r;
            if (cpp == 4)
                p[3] = 255;
        }
        else
        {
            p[0] = v.r;
            p[1] = v.g;
            p[2] = v.b;
            if (cpp == 4)
                p[3] = 255;
        }
    }
    return data;
}

void geometryTests()
{
    const auto reverse = mviewer::domain::normalizeSelection(8, 7, 2, 3, 10, 10);
    CHECK(reverse.x == 2 && reverse.y == 3 && reverse.width == 6 && reverse.height == 4,
          "reverse drag becomes canonical half-open ROI");
    const auto clipped = mviewer::domain::normalizeSelection(-4, -2, 4, 3, 10, 10);
    CHECK(clipped.x == 0 && clipped.y == 0 && clipped.width == 4 && clipped.height == 3,
          "outside ROI clips to source bounds");
    const auto one = mviewer::domain::normalizeSelection(4, 5, 5, 6, 10, 10);
    CHECK(one.x == 4 && one.y == 5 && one.width == 1 && one.height == 1,
          "one-pixel ROI remains addressable");
    const auto zero = mviewer::domain::normalizeSelection(4, 5, 4, 5, 10, 10);
    CHECK(zero.isEmpty(), "zero-area ROI is empty");
    const auto full = mviewer::domain::normalizeSelection(-100, -100, 100, 100, 10, 10);
    CHECK(full.x == 0 && full.y == 0 && full.width == 10 && full.height == 10,
          "full outside drag clips to complete source");
    const auto floating = mviewer::domain::normalizeSelection(1.2, 2.1, 3.01, 4.0, 10, 10);
    CHECK(floating.x == 1 && floating.y == 2 && floating.width == 3 && floating.height == 2,
          "floating pointer coordinates use floor/ceil pixel coverage");
}

void formatTests()
{
    const PixelFormat formats[] = {PixelFormat::RGB24, PixelFormat::RGBA32, PixelFormat::BGR24,
                                   PixelFormat::BGRA32};
    for (const auto format : formats)
    {
        const auto stats = mviewer::core::computeROIChannelStats(
            makePattern(format), mviewer::domain::Selection{0, 0, 2, 2});
        CHECK(stats.valid && stats.pixelCount == 4, "RGB-family ROI reports all pixels");
        checkNear(stats.rMean, 40.0, "RGB-family R mean is source-domain accurate");
        checkNear(stats.gMean, 50.0, "RGB-family G mean is source-domain accurate");
        checkNear(stats.bMean, 60.0, "RGB-family B mean is source-domain accurate");
        checkNear(stats.rOverG, 0.8, "R/G uses channel means");
        checkNear(stats.bOverG, 1.2, "B/G uses channel means");
        CHECK(stats.ratiosValid, "non-zero green marks ratios valid");
    }

    const auto gray = mviewer::core::computeROIChannelStats(
        makePattern(PixelFormat::Grayscale8), mviewer::domain::Selection{0, 0, 2, 2});
    CHECK(gray.valid && gray.pixelCount == 4, "grayscale ROI reports all pixels");
    checkNear(gray.rMean, 40.0, "grayscale R mean is replicated");
    checkNear(gray.gMean, 40.0, "grayscale G mean is replicated");
    checkNear(gray.bMean, 40.0, "grayscale B mean is replicated");
    checkNear(gray.rOverG, 1.0, "grayscale R/G is one");
    checkNear(gray.bOverG, 1.0, "grayscale B/G is one");

    auto zeroGreen = makeImageData(1, 1, PixelFormat::RGB24);
    (*zeroGreen.buffer)[0] = 10;
    (*zeroGreen.buffer)[1] = 0;
    (*zeroGreen.buffer)[2] = 20;
    const auto zeroStats = mviewer::core::computeROIChannelStats(
        zeroGreen, mviewer::domain::Selection{0, 0, 1, 1});
    CHECK(zeroStats.valid && !zeroStats.ratiosValid, "zero-green ROI exposes unavailable ratios");
}

void analyzerTests()
{
    const auto pixels = makePattern(PixelFormat::BGR24);
    mviewer::domain::ImageMetadata metadata;
    metadata.width = pixels.width;
    metadata.height = pixels.height;
    ImageFrame frame(metadata, pixels);
    RGBMeanAnalyzer analyzer;
    CHECK(analyzer.analyzeRegion(frame, {0, 0, 2, 1}), "RGB analyzer accepts source ROI");
    CHECK(analyzer.result().pixelCount == 2, "RGB analyzer reports ROI pixel count");
    checkNear(analyzer.result().rMean, 20.0, "RGB analyzer BGR R mean is correct");
    checkNear(analyzer.result().gMean, 30.0, "RGB analyzer BGR G mean is correct");
    checkNear(analyzer.result().bMean, 40.0, "RGB analyzer BGR B mean is correct");
    checkNear(analyzer.result().rOverG, 2.0 / 3.0, "RGB analyzer reports R/G");
    checkNear(analyzer.result().bOverG, 4.0 / 3.0, "RGB analyzer reports B/G");
    CHECK(analyzer.resultText().find("R/G") != std::string::npos,
          "RGB analyzer text includes ratio metrics");
}

void largeRegionTest()
{
    // More than two million pixels exercises 64-bit pixel accounting without
    // requiring a full 100 MP allocation in every test invocation.
    const auto large = makeImageData(2048, 1024, PixelFormat::RGBA32);
    for (size_t i = 0; i < large.buffer->size(); i += 4)
    {
        (*large.buffer)[i] = 11;
        (*large.buffer)[i + 1] = 22;
        (*large.buffer)[i + 2] = 33;
        (*large.buffer)[i + 3] = 255;
    }
    const auto stats = mviewer::core::computeROIChannelStats(
        large, mviewer::domain::Selection{0, 0, large.width, large.height});
    CHECK(stats.valid && stats.pixelCount == 2048LL * 1024LL,
          "large source ROI keeps exact 64-bit pixel count");
    checkNear(stats.rMean, 11.0, "large source R mean remains stable");
    checkNear(stats.gMean, 22.0, "large source G mean remains stable");
    checkNear(stats.bMean, 33.0, "large source B mean remains stable");
}

} // namespace

int main()
{
    geometryTests();
    formatTests();
    analyzerTests();
    largeRegionTest();
    std::printf("M60 linked ROI failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
