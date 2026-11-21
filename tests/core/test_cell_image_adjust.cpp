// Pure CellImageAdjust application: identity, geometry, and USM ordering.
#include "compareworkspace.h"
#include "core/image/CellImageAdjust.h"
#include "core/image/UsmSharpen.h"

#include <QApplication>

#include <cstring>
#include <iostream>
#include <string>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n";        \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << (msg) << "\n";                                                \
        }                                                                                          \
    } while (0)

using mviewer::core::applyCellImageAdjust;
using mviewer::core::applyUsmSharpen;
using mviewer::core::CellImageAdjust;
using mviewer::core::UsmParams;

struct CellImageAdjustTestAccess
{
    static bool defaultIdentity()
    {
        return CompareWorkspace::CellAdjust{}.isIdentity();
    }
    static bool enabledDefaultsActive()
    {
        CompareWorkspace::CellAdjust adjust;
        adjust.usmEnabled = true;
        return !adjust.isIdentity();
    }
    static bool disabledAmountIgnored()
    {
        CompareWorkspace::CellAdjust adjust;
        adjust.usmAmount = 5.0;
        return adjust.isIdentity();
    }
    static bool enabledZeroAmountIdentity()
    {
        CompareWorkspace::CellAdjust adjust;
        adjust.usmEnabled = true;
        adjust.usmAmount = 0.0;
        return adjust.isIdentity();
    }
    static ImageData apply(const ImageData &src, const CompareWorkspace::CellAdjust &adjust)
    {
        return CompareWorkspace::applyAdjusts(src, adjust);
    }
    static CompareWorkspace::CellAdjust fromCore(const CellImageAdjust &adjust)
    {
        CompareWorkspace::CellAdjust cell;
        cell.brightness = adjust.brightness;
        cell.contrast = adjust.contrast;
        cell.gamma = adjust.gamma;
        cell.rGain = adjust.rGain;
        cell.bGain = adjust.bGain;
        cell.rotation = adjust.rotation;
        cell.flipH = adjust.flipH;
        cell.flipV = adjust.flipV;
        cell.hasCrop = adjust.hasCrop;
        cell.cropX = adjust.cropX;
        cell.cropY = adjust.cropY;
        cell.cropW = adjust.cropW;
        cell.cropH = adjust.cropH;
        cell.usmEnabled = adjust.usmEnabled;
        cell.usmAmount = adjust.usmAmount;
        cell.usmRadius = adjust.usmRadius;
        cell.usmThreshold = adjust.usmThreshold;
        cell.usmLumaOnly = adjust.usmLumaOnly;
        return cell;
    }
};

static bool samePixels(const ImageData &a, const ImageData &b)
{
    return !a.isNull() && !b.isNull() && a.width == b.width && a.height == b.height &&
           a.format == b.format && a.byteSize() == b.byteSize() && a.buffer && b.buffer &&
           std::memcmp(a.buffer->data(), b.buffer->data(), a.byteSize()) == 0;
}

static void putRgb(ImageData &img, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *p =
        img.buffer->data() +
        (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

static bool eqRgb(const ImageData &img, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t *p =
        img.buffer->data() +
        (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 3;
    return p[0] == r && p[1] == g && p[2] == b;
}

static ImageData edgePattern()
{
    ImageData src = makeImageData(64, 64, PixelFormat::Grayscale8);
    for (int y = 0; y < src.height; ++y)
        for (int x = 0; x < src.width; ++x)
            src.buffer->data()[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)] =
                (x < src.width / 2) ? 80 : 180;
    return src;
}

static UsmParams usmFrom(const CellImageAdjust &adjust)
{
    UsmParams params;
    params.amount = adjust.usmAmount;
    params.radius = adjust.usmRadius;
    params.threshold = adjust.usmThreshold;
    params.lumaOnly = adjust.usmLumaOnly;
    return params;
}

static void testIdentity()
{
    ImageData src = makeImageData(8, 6, PixelFormat::RGB24);
    for (size_t i = 0; i < src.byteSize(); ++i)
        src.buffer->data()[i] = static_cast<uint8_t>((i * 13) & 0xFF);
    const ImageData out = applyCellImageAdjust(src, CellImageAdjust{});
    CHECK(samePixels(out, src), "identity adjust returns identical pixels");
    CHECK(out.buffer.get() == src.buffer.get(), "identity adjust shares the source buffer");

    CellImageAdjust disabled = {};
    disabled.usmEnabled = false;
    disabled.usmAmount = 5.0;
    const ImageData ignored = applyCellImageAdjust(src, disabled);
    CHECK(ignored.buffer.get() == src.buffer.get(), "disabled USM stays identity");
}

static void testRotation90()
{
    ImageData src = makeImageData(5, 9, PixelFormat::RGB24);
    putRgb(src, 0, 0, 9, 8, 7);
    CellImageAdjust adjust;
    adjust.rotation = 90;
    const ImageData out = applyCellImageAdjust(src, adjust);
    CHECK(!out.isNull(), "rotation 90 produces an image");
    CHECK(out.width == src.height && out.height == src.width, "rotation 90 swaps dimensions");
    CHECK(eqRgb(out, src.height - 1, 0, 9, 8, 7), "rotation 90 maps (0,0) to (h-1, 0)");
}

static void testCrop()
{
    ImageData src = makeImageData(20, 16, PixelFormat::RGB24);
    putRgb(src, 2, 3, 4, 5, 6);
    CellImageAdjust adjust;
    adjust.hasCrop = true;
    adjust.cropX = 2;
    adjust.cropY = 3;
    adjust.cropW = 10;
    adjust.cropH = 8;
    const ImageData out = applyCellImageAdjust(src, adjust);
    CHECK(!out.isNull(), "crop produces an image");
    CHECK(out.width == 10 && out.height == 8, "crop yields the crop size");
    CHECK(eqRgb(out, 0, 0, 4, 5, 6), "crop origin matches the source crop origin");
}

static void testUsmMatchesSharpenOfTonal()
{
    const ImageData src = edgePattern();
    CellImageAdjust adjust;
    adjust.brightness = 30;
    adjust.usmEnabled = true;
    adjust.usmAmount = 1.5;
    adjust.usmRadius = 1.5;
    adjust.usmThreshold = 0;
    adjust.usmLumaOnly = false;

    const ImageData out = applyCellImageAdjust(src, adjust);
    CellImageAdjust tonal = adjust;
    tonal.usmEnabled = false;
    const ImageData tonalImage = applyCellImageAdjust(src, tonal);
    const ImageData expected = applyUsmSharpen(tonalImage, usmFrom(adjust));
    CHECK(!out.isNull() && !expected.isNull(), "USM path and reference both succeed");
    CHECK(!samePixels(out, src), "USM-enabled output differs from the edge input");
    CHECK(samePixels(out, expected), "USM output equals applyUsmSharpen of the tonal image");
}

static void testWorkspaceDelegates()
{
    CHECK(CellImageAdjustTestAccess::defaultIdentity(), "default CellAdjust is identity");
    CHECK(CellImageAdjustTestAccess::enabledDefaultsActive(),
          "enabled USM defaults are not identity");
    CHECK(CellImageAdjustTestAccess::disabledAmountIgnored(), "disabled USM ignores stored amount");
    CHECK(CellImageAdjustTestAccess::enabledZeroAmountIdentity(),
          "enabled USM with zero amount is identity");

    const ImageData src = edgePattern();
    CellImageAdjust adjust;
    adjust.brightness = 12;
    adjust.usmEnabled = true;
    adjust.usmThreshold = 0;
    const ImageData viaCore = applyCellImageAdjust(src, adjust);
    const ImageData viaWorkspace =
        CellImageAdjustTestAccess::apply(src, CellImageAdjustTestAccess::fromCore(adjust));
    CHECK(samePixels(viaCore, viaWorkspace), "applyAdjusts matches applyCellImageAdjust");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testIdentity();
    testRotation90();
    testCrop();
    testUsmMatchesSharpenOfTonal();
    testWorkspaceDelegates();
    if (g_fail)
        std::cerr << g_fail << " failure(s)\n";
    return g_fail == 0 ? 0 : 1;
}
