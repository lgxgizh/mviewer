// NOLINTBEGIN
#include "core/image/rawplain/PlainRawRender.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using mviewer::core::rawplain::Cfa;
using mviewer::core::rawplain::CfaColor;
using mviewer::core::rawplain::cfaColorAt;
using mviewer::core::rawplain::computeWhiteBalance;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::renderPlainRaw;
using mviewer::core::rawplain::renderPlainRawOwned;
using mviewer::core::rawplain::setPlainRawRowsRunner;
using mviewer::core::rawplain::srgbEncode;
using mviewer::core::rawplain::WbGains;
using mviewer::core::rawplain::WhiteBalance;

namespace
{

Params makeParams(int w, int h, int bits, Packing packing, Cfa cfa)
{
    Params p;
    p.width = w;
    p.height = h;
    p.bitDepth = bits;
    p.packing = packing;
    p.blackLevel = 0;
    p.whiteLevel = 0;
    p.cfa = cfa;
    p.wb = WhiteBalance::None;
    p.wbGainR = 1.0;
    p.wbGainB = 1.0;
    p.gain = 1.0;
    p.srgbGamma = false;
    return p;
}

std::vector<std::uint16_t> filled(int w, int h, std::uint16_t value)
{
    return std::vector<std::uint16_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                                      value);
}

void paintSites(std::vector<std::uint16_t> &samples, int w, int h, Cfa cfa, int red, int green,
                int blue)
{
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const CfaColor colour = cfaColorAt(cfa, x, y);
            int value = green;
            if (colour == CfaColor::Red)
            {
                value = red;
            }
            else if (colour == CfaColor::Blue)
            {
                value = blue;
            }
            samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(value);
        }
    }
}

const std::uint8_t *pixelAt(const ImageData &image, int x, int y)
{
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                               static_cast<std::size_t>(x)) *
                              3U;
    return image.buffer->data() + index;
}

void checkCfa()
{
    const CfaColor rggb[6] = {CfaColor::Red,  CfaColor::Green, CfaColor::Green,
                              CfaColor::Blue, CfaColor::Red,   CfaColor::Blue};
    const CfaColor bggr[6] = {CfaColor::Blue, CfaColor::Green, CfaColor::Green,
                              CfaColor::Red,  CfaColor::Blue,  CfaColor::Red};
    const CfaColor grbg[6] = {CfaColor::Green, CfaColor::Red,   CfaColor::Blue,
                              CfaColor::Green, CfaColor::Green, CfaColor::Green};
    const CfaColor gbrg[6] = {CfaColor::Green, CfaColor::Blue,  CfaColor::Red,
                              CfaColor::Green, CfaColor::Green, CfaColor::Green};
    const int xs[6] = {0, 1, 0, 1, 2, 3};
    const int ys[6] = {0, 0, 1, 1, 2, 1};
    const Cfa filters[4] = {Cfa::RGGB, Cfa::BGGR, Cfa::GRBG, Cfa::GBRG};
    const CfaColor *tables[4] = {rggb, bggr, grbg, gbrg};
    for (int f = 0; f < 4; ++f)
    {
        for (int i = 0; i < 6; ++i)
        {
            if (cfaColorAt(filters[f], xs[i], ys[i]) != tables[f][i])
            {
                std::printf("cfa f=%d i=%d x=%d y=%d got=%d exp=%d\n", f, i, xs[i], ys[i],
                            static_cast<int>(cfaColorAt(filters[f], xs[i], ys[i])),
                            static_cast<int>(tables[f][i]));
                CHECK(false);
            }
        }
    }
    for (int i = 0; i < 6; ++i)
    {
        CHECK(cfaColorAt(Cfa::Mono, xs[i], ys[i]) == CfaColor::Green);
    }
}

void checkSrgb()
{
    CHECK(srgbEncode(0.0) == 0.0);
    CHECK(srgbEncode(1.0) == 1.0);
    CHECK(srgbEncode(-0.2) == 0.0);
    const double edge = 0.0031308;
    const double linearPiece = 12.92 * edge;
    const double curvePiece = 1.055 * std::pow(edge, 1.0 / 2.4) - 0.055;
    CHECK(std::fabs(srgbEncode(edge) - linearPiece) < 1e-12);
    CHECK(std::fabs(linearPiece - curvePiece) < 1e-3);
    CHECK(std::fabs(srgbEncode(0.5) - 0.7354) < 1e-3);
    double previous = -1.0;
    for (int i = 0; i <= 200; ++i)
    {
        const double y = srgbEncode(static_cast<double>(i) / 200.0);
        CHECK(y + 1e-15 >= previous);
        previous = y;
    }
}

void checkBlackGainGamma()
{
    Params p = makeParams(4, 4, 8, Packing::Bits8, Cfa::Mono);
    p.blackLevel = 16;
    p.srgbGamma = false;
    auto frame = filled(4, 4, 16);
    auto image = renderPlainRaw(p, frame, 0);
    CHECK(!image.isNull());
    CHECK(pixelAt(image, 0, 0)[0] == 0);

    frame = filled(4, 4, 255);
    image = renderPlainRaw(p, frame, 0);
    CHECK(pixelAt(image, 1, 1)[0] == 255);
    CHECK(pixelAt(image, 1, 1)[1] == 255);
    CHECK(pixelAt(image, 1, 1)[2] == 255);

    frame = filled(4, 4, 10);
    image = renderPlainRaw(p, frame, 0);
    CHECK(pixelAt(image, 0, 0)[0] == 0);

    frame = filled(4, 4, static_cast<std::uint16_t>(16 + 239 / 2));
    image = renderPlainRaw(p, frame, 0);
    const int mid = pixelAt(image, 2, 2)[0];
    CHECK(mid == 127 || mid == 128);

    p.gain = 2.0;
    image = renderPlainRaw(p, frame, 0);
    const int doubled = pixelAt(image, 0, 0)[0];
    CHECK(doubled == 254 || doubled == mid * 2 || doubled == 255);

    p.gain = 0.5;
    image = renderPlainRaw(p, frame, 0);
    const int halved = pixelAt(image, 0, 0)[0];
    CHECK(halved == 63 || halved == mid / 2);

    p.gain = 1.0;
    p.srgbGamma = true;
    image = renderPlainRaw(p, frame, 0);
    const int curved = pixelAt(image, 0, 0)[0];
    CHECK(curved > mid);
}

void checkNeutral(const ImageData &image)
{
    CHECK(!image.isNull());
    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint8_t *px = pixelAt(image, x, y);
            const int dr = std::abs(static_cast<int>(px[0]) - static_cast<int>(px[1]));
            const int db = std::abs(static_cast<int>(px[2]) - static_cast<int>(px[1]));
            CHECK(dr <= 1);
            CHECK(db <= 1);
        }
    }
}

double leastGain(const WbGains &gains)
{
    double least = gains.r;
    if (gains.g < least)
    {
        least = gains.g;
    }
    if (gains.b < least)
    {
        least = gains.b;
    }
    return least;
}

void checkWhiteBalance()
{
    const Cfa filters[4] = {Cfa::RGGB, Cfa::BGGR, Cfa::GRBG, Cfa::GBRG};
    for (Cfa cfa : filters)
    {
        Params p = makeParams(32, 24, 10, Packing::Lsb16, cfa);
        auto samples = filled(32, 24, 0);
        paintSites(samples, 32, 24, cfa, 200, 400, 100);
        p.wb = WhiteBalance::None;
        WbGains none = computeWhiteBalance(p, samples);
        CHECK(none.r == 1.0 && none.g == 1.0 && none.b == 1.0);

        p.wb = WhiteBalance::GrayWorld;
        WbGains gray = computeWhiteBalance(p, samples);
        CHECK(std::fabs(leastGain(gray) - 1.0) < 1e-9);
        CHECK(std::fabs(gray.r / gray.g - 2.0) < 1e-6);
        CHECK(std::fabs(gray.b / gray.g - 4.0) < 1e-6);

        p.srgbGamma = true;
        checkNeutral(renderPlainRaw(p, samples, 0));

        p.wb = WhiteBalance::Manual;
        p.wbGainR = 1.25;
        p.wbGainB = 3.5;
        WbGains manual = computeWhiteBalance(p, samples);
        CHECK(std::fabs(leastGain(manual) - 1.0) < 1e-12);
        CHECK(std::fabs(manual.r / manual.g - 1.25) < 1e-9);
        CHECK(std::fabs(manual.b / manual.g - 3.5) < 1e-9);
    }

    Params patch = makeParams(100, 100, 10, Packing::Lsb16, Cfa::RGGB);
    auto scene = filled(100, 100, 0);
    paintSites(scene, 100, 100, Cfa::RGGB, 100, 200, 50);
    for (int y = 0; y < 20; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            const CfaColor colour = cfaColorAt(Cfa::RGGB, x, y);
            int value = 900;
            if (colour == CfaColor::Red)
            {
                value = 800;
            }
            else if (colour == CfaColor::Blue)
            {
                value = 700;
            }
            scene[static_cast<std::size_t>(y) * 100U + static_cast<std::size_t>(x)] =
                static_cast<std::uint16_t>(value);
        }
    }
    patch.wb = WhiteBalance::WhitePatch;
    WbGains bright = computeWhiteBalance(patch, scene);
    CHECK(std::fabs(leastGain(bright) - 1.0) < 1e-9);
    CHECK(std::fabs(bright.r / bright.g - (900.0 / 800.0)) < 1e-6);
    CHECK(std::fabs(bright.b / bright.g - (900.0 / 700.0)) < 1e-6);

    Params clipped = makeParams(16, 16, 10, Packing::Lsb16, Cfa::RGGB);
    auto raw = filled(16, 16, 0);
    paintSites(raw, 16, 16, Cfa::RGGB, 200, 400, 100);
    for (int y = 0; y < 16; y += 2)
    {
        raw[static_cast<std::size_t>(y) * 16U] = 1023;
    }
    clipped.wb = WhiteBalance::GrayWorld;
    WbGains ignored = computeWhiteBalance(clipped, raw);
    CHECK(std::fabs(leastGain(ignored) - 1.0) < 1e-9);
    CHECK(std::fabs(ignored.r / ignored.g - 2.0) < 1e-6);
    CHECK(std::fabs(ignored.b / ignored.g - 4.0) < 1e-6);

    Params mono = makeParams(8, 8, 10, Packing::Lsb16, Cfa::Mono);
    mono.wb = WhiteBalance::GrayWorld;
    auto monoSamples = filled(8, 8, 50);
    WbGains monoGains = computeWhiteBalance(mono, monoSamples);
    CHECK(monoGains.r == 1.0 && monoGains.g == 1.0 && monoGains.b == 1.0);

    Params zeros = makeParams(8, 8, 10, Packing::Lsb16, Cfa::RGGB);
    zeros.wb = WhiteBalance::GrayWorld;
    WbGains zeroGains = computeWhiteBalance(zeros, filled(8, 8, 0));
    CHECK(zeroGains.r == 1.0 && zeroGains.g == 1.0 && zeroGains.b == 1.0);

    Params extreme = makeParams(8, 8, 10, Packing::Lsb16, Cfa::RGGB);
    extreme.wb = WhiteBalance::GrayWorld;
    auto uneven = filled(8, 8, 0);
    paintSites(uneven, 8, 8, Cfa::RGGB, 10, 400, 100);
    WbGains clamped = computeWhiteBalance(extreme, uneven);
    CHECK(std::fabs(leastGain(clamped) - 1.0) < 1e-9);
    CHECK(std::fabs(clamped.r / clamped.g - 16.0) < 1e-6);
    CHECK(std::fabs(clamped.b / clamped.g - 4.0) < 1e-6);
}

void checkClippedHighlights()
{
    Params warm = makeParams(32, 24, 10, Packing::Lsb16, Cfa::RGGB);
    warm.wb = WhiteBalance::GrayWorld;
    auto warmSamples = filled(32, 24, 0);
    paintSites(warmSamples, 32, 24, Cfa::RGGB, 400, 200, 200);
    const WbGains warmGains = computeWhiteBalance(warm, warmSamples);
    CHECK(std::fabs(warmGains.r - 1.0) < 1e-6);
    CHECK(std::fabs(warmGains.g - 2.0) < 1e-6);
    CHECK(std::fabs(warmGains.b - 2.0) < 1e-6);

    Params blown = makeParams(8, 8, 8, Packing::Bits8, Cfa::RGGB);
    blown.wb = WhiteBalance::Manual;
    blown.wbGainR = 0.5;
    blown.wbGainB = 2.0;
    const ImageData image = renderPlainRaw(blown, filled(8, 8, 255), 0);
    CHECK(!image.isNull());
    if (image.isNull())
    {
        return;
    }
    const std::uint8_t *px = pixelAt(image, 3, 3);
    CHECK(px[0] == 255);
    CHECK(px[1] == 255);
    CHECK(px[2] == 255);
}

void expectColour(const ImageData &image, int r, int g, int b)
{
    CHECK(!image.isNull());
    CHECK(image.format == PixelFormat::RGB24);
    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint8_t *px = pixelAt(image, x, y);
            CHECK(px[0] == static_cast<std::uint8_t>(r));
            CHECK(px[1] == static_cast<std::uint8_t>(g));
            CHECK(px[2] == static_cast<std::uint8_t>(b));
        }
    }
}

void checkDemosaic()
{
    const Cfa filters[4] = {Cfa::RGGB, Cfa::BGGR, Cfa::GRBG, Cfa::GBRG};
    const int widths[3] = {2, 3, 8};
    for (Cfa cfa : filters)
    {
        for (int n : widths)
        {
            Params p = makeParams(n, n, 8, Packing::Bits8, cfa);
            auto samples = filled(n, n, 0);
            paintSites(samples, n, n, cfa, 40, 90, 20);
            expectColour(renderPlainRaw(p, samples, 0), 40, 90, 20);
        }
    }

    Params ramp = makeParams(32, 4, 8, Packing::Bits8, Cfa::RGGB);
    auto samples = filled(32, 4, 0);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            if (cfaColorAt(Cfa::RGGB, x, y) == CfaColor::Green)
            {
                samples[static_cast<std::size_t>(y) * 32U + static_cast<std::size_t>(x)] =
                    static_cast<std::uint16_t>(x * 4);
            }
        }
    }
    auto image = renderPlainRaw(ramp, samples, 0);
    CHECK(!image.isNull());
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 1; x < 32; ++x)
        {
            CHECK(pixelAt(image, x - 1, y)[1] <= pixelAt(image, x, y)[1]);
        }
    }

    const int thinW[4] = {1, 2, 3, 1};
    const int thinH[4] = {1, 2, 3, 5};
    for (int i = 0; i < 4; ++i)
    {
        Params p = makeParams(thinW[i], thinH[i], 8, Packing::Bits8, Cfa::BGGR);
        auto tiny = filled(thinW[i], thinH[i], 30);
        auto out = renderPlainRaw(p, tiny, 0);
        CHECK(!out.isNull());
        CHECK(out.width == thinW[i]);
        CHECK(out.height == thinH[i]);
        CHECK(out.buffer->size() ==
              static_cast<std::size_t>(thinW[i]) * static_cast<std::size_t>(thinH[i]) * 3U);
    }
    Params row = makeParams(7, 1, 8, Packing::Bits8, Cfa::GRBG);
    auto rowSamples = filled(7, 1, 12);
    auto rowImage = renderPlainRaw(row, rowSamples, 0);
    CHECK(rowImage.buffer->size() == 7U * 3U);
    Params col = makeParams(1, 9, 8, Packing::Bits8, Cfa::GBRG);
    auto colImage = renderPlainRaw(col, filled(1, 9, 12), 0);
    CHECK(colImage.buffer->size() == 9U * 3U);
}

void checkPreviewAndInvalid()
{
    Params p = makeParams(400, 300, 8, Packing::Bits8, Cfa::RGGB);
    auto samples = filled(400, 300, 0);
    paintSites(samples, 400, 300, Cfa::RGGB, 30, 60, 90);
    auto preview = renderPlainRaw(p, samples, 64);
    CHECK(preview.width == 64);
    CHECK(preview.height == 48);
    expectColour(preview, 30, 60, 90);

    auto full = renderPlainRaw(p, samples, 400);
    CHECK(full.width == 400);
    CHECK(full.height == 300);
    expectColour(full, 30, 60, 90);
    auto larger = renderPlainRaw(p, samples, 1000);
    CHECK(larger.width == 400 && larger.height == 300);

    Params bad = p;
    bad.width = 0;
    CHECK(renderPlainRaw(bad, samples, 0).isNull());
    CHECK(renderPlainRaw(p, filled(10, 10, 1), 0).isNull());
    bad = p;
    bad.blackLevel = 300;
    CHECK(renderPlainRaw(bad, samples, 0).isNull());
}

void checkTiming()
{
    constexpr int kW = 4096;
    constexpr int kH = 3072;
    Params p = makeParams(kW, kH, 10, Packing::Lsb16, Cfa::RGGB);
    p.srgbGamma = true;
    p.wb = WhiteBalance::None;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH));
    std::uint32_t state = 0x12345678U;
    for (std::uint16_t &sample : samples)
    {
        state = state * 1664525U + 1013904223U;
        sample = static_cast<std::uint16_t>(state & 1023U);
    }
    const auto start = std::chrono::steady_clock::now();
    auto image = renderPlainRaw(p, samples, 0);
    const auto stop = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::printf("timing 4096x3072: %lld ms\n", static_cast<long long>(ms));
    CHECK(!image.isNull());
    CHECK(image.width == kW);
    CHECK(image.height == kH);
    CHECK(image.buffer->size() == static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH) * 3U);
}

bool nearGain(double got, double want)
{
    const double scale = std::fabs(want) > 1.0 ? std::fabs(want) : 1.0;
    return std::fabs(got - want) <= scale * 0.01;
}

void checkPreviewAverage()
{
    constexpr int kW = 64;
    constexpr int kH = 32;
    Params p = makeParams(kW, kH, 8, Packing::Bits8, Cfa::RGGB);
    auto samples = filled(kW, kH, 0);
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const bool alt = (((x / 2) + (y / 2)) & 1) != 0;
            const int red = alt ? 200 : 40;
            const int green = alt ? 180 : 20;
            const int blue = alt ? 100 : 60;
            const CfaColor colour = cfaColorAt(Cfa::RGGB, x, y);
            int value = green;
            if (colour == CfaColor::Red)
            {
                value = red;
            }
            else if (colour == CfaColor::Blue)
            {
                value = blue;
            }
            const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(kW);
            samples[row + static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(value);
        }
    }
    const ImageData preview = renderPlainRaw(p, samples, 8);
    CHECK(preview.width == 8);
    CHECK(preview.height == 4);
    CHECK(!preview.isNull());
    if (preview.isNull())
    {
        return;
    }
    for (int y = 0; y < preview.height; ++y)
    {
        for (int x = 0; x < preview.width; ++x)
        {
            const std::uint8_t *px = pixelAt(preview, x, y);
            CHECK(std::abs(static_cast<int>(px[0]) - 120) <= 2);
            CHECK(std::abs(static_cast<int>(px[1]) - 100) <= 2);
            CHECK(std::abs(static_cast<int>(px[2]) - 80) <= 2);
            CHECK(px[0] != 40 && px[0] != 200);
            CHECK(px[1] != 20 && px[1] != 180);
            CHECK(px[2] != 60 && px[2] != 100);
        }
    }
}

void checkSubsampledWhiteBalance()
{
    constexpr int kW = 2448;
    constexpr int kH = 2048;
    Params smallP = makeParams(32, 24, 10, Packing::Lsb16, Cfa::RGGB);
    smallP.wb = WhiteBalance::GrayWorld;
    auto smallSamples = filled(32, 24, 0);
    paintSites(smallSamples, 32, 24, Cfa::RGGB, 200, 400, 100);
    const WbGains full = computeWhiteBalance(smallP, smallSamples);

    Params largeP = makeParams(kW, kH, 10, Packing::Lsb16, Cfa::RGGB);
    largeP.wb = WhiteBalance::GrayWorld;
    auto largeSamples = filled(kW, kH, 0);
    paintSites(largeSamples, kW, kH, Cfa::RGGB, 200, 400, 100);
    const WbGains sub = computeWhiteBalance(largeP, largeSamples);

    CHECK(std::fabs(leastGain(full) - 1.0) < 1e-9);
    CHECK(std::fabs(full.r / full.g - 2.0) < 1e-6);
    CHECK(std::fabs(full.b / full.g - 4.0) < 1e-6);
    CHECK(nearGain(sub.r, full.r));
    CHECK(nearGain(sub.g, full.g));
    CHECK(nearGain(sub.b, full.b));
}

bool sameRgb(const ImageData &a, const ImageData &b)
{
    if (a.isNull() || b.isNull() || !a.buffer || !b.buffer)
    {
        return false;
    }
    if (a.width != b.width || a.height != b.height)
    {
        return false;
    }
    return *a.buffer == *b.buffer;
}

void threeBandRunner(int rowCount, const std::function<void(int, int)> &fn)
{
    constexpr int kBands = 3;
    for (int band = 0; band < kBands; ++band)
    {
        const int y0 = rowCount * band / kBands;
        const int y1 = rowCount * (band + 1) / kBands;
        fn(y0, y1);
    }
}

std::vector<std::uint16_t> rampFrame(int w, int h)
{
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int value = (x * 3 + y * 5) & 255;
            const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
            samples[row + static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(value);
        }
    }
    return samples;
}

void checkOwnedAndRowBands()
{
    Params p = makeParams(64, 32, 8, Packing::Bits8, Cfa::RGGB);
    const auto samples = rampFrame(64, 32);
    setPlainRawRowsRunner(nullptr);
    const ImageData serial = renderPlainRaw(p, samples, 0);
    const ImageData owned = renderPlainRawOwned(p, std::vector<std::uint16_t>(samples), 0);
    CHECK(sameRgb(serial, owned));

    setPlainRawRowsRunner(&threeBandRunner);
    const ImageData banded = renderPlainRaw(p, samples, 0);
    const ImageData bandedOwned = renderPlainRawOwned(p, std::vector<std::uint16_t>(samples), 0);
    CHECK(sameRgb(serial, banded));
    CHECK(sameRgb(serial, bandedOwned));

    setPlainRawRowsRunner(nullptr);
    Params mono = makeParams(64, 32, 8, Packing::Bits8, Cfa::Mono);
    const ImageData monoSerial = renderPlainRaw(mono, samples, 0);
    setPlainRawRowsRunner(&threeBandRunner);
    CHECK(sameRgb(monoSerial, renderPlainRaw(mono, samples, 0)));
    setPlainRawRowsRunner(nullptr);
}

} // namespace

int main()
{
    checkCfa();
    checkSrgb();
    checkBlackGainGamma();
    checkWhiteBalance();
    checkClippedHighlights();
    checkDemosaic();
    checkPreviewAndInvalid();
    checkPreviewAverage();
    checkSubsampledWhiteBalance();
    checkOwnedAndRowBands();
    checkTiming();
    if (g_failures != 0)
    {
        std::printf("test_plainraw_render: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_plainraw_render: all passed\n");
    return 0;
}
// NOLINTEND
