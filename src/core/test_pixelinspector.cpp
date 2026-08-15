#include "core/analysis/PixelInspector.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageAdjust.h"
#include "core/image/ImageFrame.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace mviewer::core;

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL: %s @line %d\n", #cond, __LINE__);                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static void test_color_spaces()
{
    // Pure red: RGB(255,0,0).
    auto rgb = toColorSpace(255, 0, 0, ColorSpace::RGB);
    CHECK(std::abs(rgb.c1 - 255) < 1e-6 && std::abs(rgb.c2) < 1e-6 && std::abs(rgb.c3) < 1e-6);

    auto hsv = toColorSpace(255, 0, 0, ColorSpace::HSV);
    CHECK(std::abs(hsv.c1 - 0) < 1e-6);   // hue 0
    CHECK(std::abs(hsv.c3 - 100) < 1e-6); // value 100

    // White → Lab L≈100, a≈0, b≈0.
    auto lab = toColorSpace(255, 255, 255, ColorSpace::Lab);
    CHECK(std::abs(lab.c1 - 100) < 1e-3);
    CHECK(std::abs(lab.c2) < 1e-3);
    CHECK(std::abs(lab.c3) < 1e-3);

    // Black → Lab L≈0.
    auto labk = toColorSpace(0, 0, 0, ColorSpace::Lab);
    CHECK(std::abs(labk.c1) < 1e-3);

    // YUV: gray (128,128,128) → Y≈128, U≈0, V≈0.
    auto yuv = toColorSpace(128, 128, 128, ColorSpace::YUV);
    CHECK(std::abs(yuv.c1 - 128) < 1e-3);
    CHECK(std::abs(yuv.c2) < 1e-3);
    CHECK(std::abs(yuv.c3) < 1e-3);

    // YCbCr: gray (128,128,128) → Y≈128, Cb≈128, Cr≈128.
    auto yc = toColorSpace(128, 128, 128, ColorSpace::YCbCr);
    CHECK(std::abs(yc.c1 - 128) < 1e-3);
    CHECK(std::abs(yc.c2 - 128) < 1e-3);
    CHECK(std::abs(yc.c3 - 128) < 1e-3);
}

static void test_neighborhood()
{
    // 3×3 image, solid value 100 → every stat must equal 100/0.
    const int w = 3, h = 3;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 3, 100); // gray, lum=100
    auto s = neighborhoodStats(buf.data(), w * 3, w, h, 1, 1, 3);
    CHECK(s.count == 9);
    CHECK(std::abs(s.mean - 100) < 1e-6);
    CHECK(std::abs(s.variance) < 1e-6);
    CHECK(std::abs(s.stdDev) < 1e-6);
    CHECK(s.min == 100 && s.max == 100);

    // Gradient: increasing rows 0,10,20 → lum 0,10,20 at the three rows.
    std::vector<uint8_t> g(static_cast<size_t>(w) * h * 3, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t v = static_cast<uint8_t>(y * 10);
            size_t i = (static_cast<size_t>(y) * w + x) * 3;
            g[i] = g[i + 1] = g[i + 2] = v;
        }
    auto sg = neighborhoodStats(g.data(), w * 3, w, h, 1, 1, 3);
    // 3×3 around the center row samples {0,0,0, 10,10,10, 20,20,20} → mean 10.
    CHECK(std::abs(sg.mean - 10) < 1e-6);
    CHECK(sg.min == 0 && sg.max == 20);
    CHECK(sg.count == 9);
    // population variance = (3·0² + 3·10² + 3·20²)/9 − 10² = 66.67
    const double expectedVar = (3.0 * (0 - 10) * (0 - 10) + 3.0 * (20 - 10) * (20 - 10)) / 9.0;
    CHECK(std::abs(sg.variance - expectedVar) < 1e-6);

    // Out-of-bounds center must be clipped (1×1 at corner = single pixel).
    auto corner = neighborhoodStats(g.data(), w * 3, w, h, 0, 0, 1);
    CHECK(corner.count == 1);
    CHECK(corner.mean == 0);
}

static void setRgb(ImageData &image, int x, int y, int r, int g, int b)
{
    ImageBuffer view = image.view();
    uint8_t *pixel = view.data + static_cast<size_t>(y) * view.stride() +
                     static_cast<size_t>(x) * view.channelsPerPixel();
    pixel[0] = static_cast<uint8_t>(r);
    pixel[1] = static_cast<uint8_t>(g);
    pixel[2] = static_cast<uint8_t>(b);
}

static int coordinateValue(int x, int y)
{
    return 10 + x * 3 + y * 30;
}

static void checkSourceSample(const ImageData &image, const AnalysisAdjustment &adjustment, int x,
                              int y, int sourceX, int sourceY)
{
    const auto actual = sampleAnalysisPixel(image, adjustment, x, y);
    const int value = coordinateValue(sourceX, sourceY);
    CHECK(actual.valid && actual.r == value && actual.g == value + 1 && actual.b == value + 2);
}

static void test_source_backed_analysis()
{
    // A coordinate-coded 3x2 source makes crop and rotation inverse mappings
    // observable without depending on any widget or display-scale behavior.
    ImageData source = makeImageData(3, 2, PixelFormat::RGB24);
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x)
        {
            const int value = coordinateValue(x, y);
            setRgb(source, x, y, value, value + 1, value + 2);
        }

    AnalysisAdjustment identity;
    checkSourceSample(source, identity, 2, 1, 2, 1);
    CHECK(!sampleAnalysisPixel(source, identity, 3, 0).valid);

    AnalysisAdjustment crop = identity;
    crop.hasCrop = true;
    crop.cropX = 1;
    crop.cropY = 0;
    crop.cropW = 2;
    crop.cropH = 2;
    checkSourceSample(source, crop, 0, 0, 1, 0);
    checkSourceSample(source, crop, 1, 1, 2, 1);

    crop.rotation = 90;
    checkSourceSample(source, crop, 0, 0, 1, 1);
    checkSourceSample(source, crop, 1, 0, 1, 0);
    checkSourceSample(source, crop, 0, 1, 2, 1);
    checkSourceSample(source, crop, 1, 1, 2, 0);
    crop.rotation = 180;
    checkSourceSample(source, crop, 0, 0, 2, 1);
    crop.rotation = 270;
    checkSourceSample(source, crop, 0, 0, 2, 0);

    ImageData adjustedSource = makeImageData(1, 1, PixelFormat::RGB24);
    setRgb(adjustedSource, 0, 0, 90, 140, 210);
    AnalysisAdjustment adjustment;
    adjustment.brightness = 17;
    adjustment.contrast = 1.25;
    adjustment.gamma = 1.4;
    adjustment.redGain = 1.3;
    adjustment.blueGain = 0.8;
    const ImageData expected = adjustWhiteBalance(
        adjustGamma(adjustContrast(adjustBrightness(adjustedSource, adjustment.brightness),
                                   static_cast<float>(adjustment.contrast)),
                    static_cast<float>(adjustment.gamma)),
        static_cast<float>(adjustment.redGain), static_cast<float>(adjustment.blueGain));
    const auto expectedPixel = samplePixel(expected, 0, 0);
    const auto actualPixel = sampleAnalysisPixel(adjustedSource, adjustment, 0, 0);
    CHECK(actualPixel.valid && actualPixel.r == expectedPixel.r && actualPixel.g == expectedPixel.g &&
          actualPixel.b == expectedPixel.b);

    ImageData grayscale = makeImageData(1, 1, PixelFormat::Grayscale8);
    grayscale.buffer->at(0) = 120;
    adjustment = AnalysisAdjustment{};
    adjustment.redGain = 2.0;
    adjustment.blueGain = 0.5;
    const auto grayscalePixel = sampleAnalysisPixel(grayscale, adjustment, 0, 0);
    CHECK(grayscalePixel.valid && grayscalePixel.r == 120 && grayscalePixel.g == 120 &&
          grayscalePixel.b == 120);

    ImageData neighborhood = makeImageData(9, 9, PixelFormat::Grayscale8);
    for (int y = 0; y < neighborhood.height; ++y)
        for (int x = 0; x < neighborhood.width; ++x)
            neighborhood.buffer->at(static_cast<size_t>(y) * neighborhood.width + x) =
                static_cast<uint8_t>(y * neighborhood.width + x);

    for (const int kernel : {1, 3, 5, 7})
    {
        const auto stats = neighborhoodStats(neighborhood, identity, 4, 4, kernel);
        long long expectedSum = 0;
        const int half = kernel / 2;
        for (int y = 4 - half; y <= 4 + half; ++y)
            for (int x = 4 - half; x <= 4 + half; ++x)
                expectedSum += y * neighborhood.width + x;
        CHECK(stats.count == kernel * kernel);
        CHECK(std::abs(stats.mean - static_cast<double>(expectedSum) / stats.count) < 1e-9);
    }
    CHECK(neighborhoodStats(neighborhood, identity, 0, 0, 7).count == 16);
}

static void test_raw16At()
{
    // ImageFrame::raw16At is the source of truth for the Pixel Inspector
    // high-bit-depth readout. Build an ImageFrame from a synthetic 2x2 RGB24
    // buffer and attach a synthetic 16-bit sample buffer.
    std::vector<uint8_t> px(2 * 2 * 3, 0);
    ImageData data;
    data.width = 2;
    data.height = 2;
    data.format = PixelFormat::RGB24;
    data.buffer = std::make_shared<std::vector<uint8_t>>(px);

    ImageFrame frame;
    frame.setPixels(data);

    std::vector<uint16_t> buf(2 * 2 * 3);
    // pixel (0,0) = (100,200,300); (1,0) = (1000,2000,3000); (0,1) = (11,22,33)
    buf[0] = 100;
    buf[1] = 200;
    buf[2] = 300;
    buf[3] = 1000;
    buf[4] = 2000;
    buf[5] = 3000;
    buf[6] = 11;
    buf[7] = 22;
    buf[8] = 33;
    frame.setRaw16(std::make_shared<std::vector<uint16_t>>(buf), 65535, 3);
    CHECK(frame.hasRaw16());
    CHECK(frame.raw16Max() == 65535);
    CHECK(frame.raw16Channels() == 3);

    uint16_t r = 0, g = 0, b = 0;
    CHECK(frame.raw16At(0, 0, r, g, b));
    CHECK(r == 100 && g == 200 && b == 300);
    CHECK(frame.raw16At(1, 0, r, g, b));
    CHECK(r == 1000 && g == 2000 && b == 3000);
    CHECK(frame.raw16At(0, 1, r, g, b));
    CHECK(r == 11 && g == 22 && b == 33);

    // out of range must be rejected
    CHECK(!frame.raw16At(5, 5, r, g, b));
    CHECK(!frame.raw16At(-1, 0, r, g, b));

    // grayscale 16-bit (channels=1) duplicates the single sample to R/G/B
    std::vector<uint16_t> gb(2 * 2);
    gb[0] = 4242;
    frame.setRaw16(std::make_shared<std::vector<uint16_t>>(gb), 65535, 1);
    CHECK(frame.raw16Channels() == 1);
    CHECK(frame.raw16At(0, 0, r, g, b));
    CHECK(r == 4242 && g == 4242 && b == 4242);
}

static void test_color_spaces_16bit()
{
    // RGB returns the raw 16-bit integer sample.
    auto rgb = toColorSpace(65535, 0, 0, 65535, ColorSpace::RGB);
    CHECK(std::abs(rgb.c1 - 65535) < 1e-6 && std::abs(rgb.c2) < 1e-6 && std::abs(rgb.c3) < 1e-6);

    // Full-scale pure red matches the 8-bit pure-red readouts (hue 0, value 100).
    auto hsv = toColorSpace(65535, 0, 0, 65535, ColorSpace::HSV);
    CHECK(std::abs(hsv.c1) < 1e-6 && std::abs(hsv.c3 - 100) < 1e-6);

    // At the same normalized level a 16-bit sample must agree with its 8-bit
    // equivalent; the only difference is finer quantization, so a small
    // tolerance absorbs the 8→16 upscale rounding.
    const uint8_t v8 = 128;
    const uint16_t v16 = static_cast<uint16_t>(std::round(v8 * 65535.0 / 255.0));
    for (auto space :
         {ColorSpace::HSV, ColorSpace::Lab, ColorSpace::YUV, ColorSpace::YCbCr, ColorSpace::XYZ})
    {
        const auto a = toColorSpace(v8, v8, v8, space);
        const auto b = toColorSpace(v16, v16, v16, 65535, space);
        CHECK(std::abs(a.c1 - b.c1) < 1e-1);
        CHECK(std::abs(a.c2 - b.c2) < 1e-1);
        CHECK(std::abs(a.c3 - b.c3) < 1e-1);
    }

    // HEX maps the 16-bit sample down to 8-bit (full-scale red → 255 channel).
    auto hexT = toColorSpace(65535, 0, 0, 65535, ColorSpace::HEX);
    CHECK(std::abs(hexT.c1 - 255) < 1e-6 && std::abs(hexT.c2) < 1e-6 && std::abs(hexT.c3) < 1e-6);
}

int main()
{
    test_color_spaces();
    test_neighborhood();
    test_source_backed_analysis();
    test_raw16At();
    test_color_spaces_16bit();
    if (g_failures == 0)
    {
        std::printf("PASS: pixelinspector_tests (%d checks)\n", 0);
        return 0;
    }
    std::printf("FAILED: pixelinspector_tests (%d failures)\n", g_failures);
    return 1;
}
