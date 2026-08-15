#include "core/analysis/PixelInspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mviewer::core
{
namespace
{
inline double clamp01(double x)
{
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

// Rec. 709 luma — the luminance used for neighborhood stats.
inline double luma(double r, double g, double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b; // r,g,b in 0..1
}
} // namespace

namespace
{
// Chromaticity math for HSV/Lab/YUV/YCbCr/XYZ given channels normalized to
// 0..1. Returns each channel in the same human-readable range the 8-bit
// front-ends use (HSV 0..360/0..100/0..100, Lab 0..100/-128..127,
// YUV Y 0..255 / U,V -128..127, YCbCr 0..255, XYZ ~0..1). RGB/HEX are handled
// by the public overloads because their ranges depend on the source bit depth.
ColorTriple toColorSpaceNorm(double R, double G, double B, ColorSpace space)
{
    ColorTriple out;
    switch (space)
    {
    case ColorSpace::HSV:
    {
        const double mx = std::max({R, G, B});
        const double mn = std::min({R, G, B});
        const double d = mx - mn;
        double h = 0.0;
        if (d > 1e-9)
        {
            if (mx == R)
                h = std::fmod(60.0 * ((G - B) / d), 360.0);
            else if (mx == G)
                h = 60.0 * ((B - R) / d + 2.0);
            else
                h = 60.0 * ((R - G) / d + 4.0);
            if (h < 0.0)
                h += 360.0;
        }
        const double v = mx;
        const double s = mx > 1e-9 ? d / mx : 0.0;
        out.c1 = h;
        out.c2 = s * 100.0;
        out.c3 = v * 100.0;
        break;
    }

    case ColorSpace::Lab:
    {
        // sRGB → linear → XYZ (D65) → Lab.
        auto toLin = [](double c)
        { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        const double lr = toLin(R), lg = toLin(G), lb = toLin(B);
        const double X = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / 0.95047;
        const double Y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / 1.00000;
        const double Z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / 1.08883;
        auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0); };
        const double fx = f(X), fy = f(Y), fz = f(Z);
        out.c1 = 116.0 * fy - 16.0;
        out.c2 = 500.0 * (fx - fy);
        out.c3 = 200.0 * (fy - fz);
        break;
    }

    case ColorSpace::YUV:
    {
        // BT.601, Y in 0..255, U/V in -128..127. Scaled so a neutral
        // gray (R=G=B) yields exactly U=V=0.
        const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
        out.c1 = Y * 255.0;
        out.c2 = 0.564 * (B - Y) * 255.0; // 0.564 ≈ 0.5 / (1 - 0.114)
        out.c3 = 0.713 * (R - Y) * 255.0; // 0.713 ≈ 0.5 / (1 - 0.299)
        break;
    }

    case ColorSpace::YCbCr:
    {
        // BT.601, full range, Y/Cb/Cr in 0..255.
        out.c1 = (0.299 * R + 0.587 * G + 0.114 * B) * 255.0;
        out.c2 = (-0.168736 * R - 0.331264 * G + 0.5 * B) * 255.0 + 128.0;
        out.c3 = (0.5 * R - 0.418688 * G - 0.081312 * B) * 255.0 + 128.0;
        break;
    }

    case ColorSpace::XYZ:
    {
        // sRGB → linear → CIE XYZ (D65).
        auto toLin = [](double c)
        { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        const double lr = toLin(R), lg = toLin(G), lb = toLin(B);
        out.c1 = lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375; // X
        out.c2 = lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750; // Y
        out.c3 = lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041; // Z
        break;
    }

    default:
        break;
    }
    return out;
}
} // namespace

ColorTriple toColorSpace(uint8_t r, uint8_t g, uint8_t b, ColorSpace space)
{
    if (space == ColorSpace::RGB || space == ColorSpace::HEX)
        return {double(r), double(g), double(b)};
    return toColorSpaceNorm(r / 255.0, g / 255.0, b / 255.0, space);
}

ColorTriple toColorSpace(uint16_t r, uint16_t g, uint16_t b, uint16_t maxVal, ColorSpace space)
{
    if (maxVal == 0)
        maxVal = 1;
    if (space == ColorSpace::RGB)
        return {double(r), double(g), double(b)};
    if (space == ColorSpace::HEX)
    {
        const auto map = [maxVal](uint16_t v)
        { return static_cast<uint8_t>(std::min<double>(255.0, v * 255.0 / maxVal + 0.5)); };
        return {double(map(r)), double(map(g)), double(map(b))};
    }
    return toColorSpaceNorm(double(r) / maxVal, double(g) / maxVal, double(b) / maxVal, space);
}

namespace
{
struct CropBounds
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

CropBounds analysisCropBounds(const ImageData &source, const AnalysisAdjustment &adjustment)
{
    CropBounds bounds{0, 0, source.width, source.height};
    if (!adjustment.hasCrop || adjustment.cropW <= 0 || adjustment.cropH <= 0)
        return bounds;

    const auto clampCoordinate = [](long long value, int limit)
    { return static_cast<int>(std::clamp(value, 0LL, static_cast<long long>(limit))); };
    bounds.x = clampCoordinate(adjustment.cropX, source.width);
    bounds.y = clampCoordinate(adjustment.cropY, source.height);
    const int right = clampCoordinate(static_cast<long long>(adjustment.cropX) + adjustment.cropW,
                                      source.width);
    const int bottom = clampCoordinate(static_cast<long long>(adjustment.cropY) + adjustment.cropH,
                                       source.height);
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
    return bounds;
}

int normalizedRotation(int rotation)
{
    rotation %= 360;
    if (rotation < 0)
        rotation += 360;
    return rotation;
}

int adjustAnalysisChannel(int value, const AnalysisAdjustment &adjustment)
{
    if (adjustment.brightness != 0)
        value = std::clamp(value + std::clamp(adjustment.brightness, -255, 255), 0, 255);

    const float contrast = static_cast<float>(adjustment.contrast);
    if (std::abs(contrast - 1.0f) >= 1e-6f)
    {
        const float v = (static_cast<float>(value) - 128.0f) * std::max(contrast, 0.0f) + 128.0f;
        value = std::clamp(static_cast<int>(std::lroundf(v)), 0, 255);
    }

    const float gamma = static_cast<float>(adjustment.gamma);
    if (std::abs(gamma - 1.0f) >= 1e-6f)
    {
        const float clampedGamma = std::clamp(gamma, 0.05f, 8.0f);
        const float corrected =
            std::pow(static_cast<float>(value) / 255.0f, 1.0f / clampedGamma);
        value = static_cast<int>(std::lroundf(corrected * 255.0f));
    }
    return std::clamp(value, 0, 255);
}
} // namespace

AnalysisPixel sampleAnalysisPixel(const ImageData &source, const AnalysisAdjustment &adjustment,
                                  int adjustedX, int adjustedY)
{
    AnalysisPixel result;
    if (source.isNull())
        return result;

    const CropBounds crop = analysisCropBounds(source, adjustment);
    if (crop.width <= 0 || crop.height <= 0)
        return result;

    const int rotation = normalizedRotation(adjustment.rotation);
    const int outputWidth = (rotation == 90 || rotation == 270) ? crop.height : crop.width;
    const int outputHeight = (rotation == 90 || rotation == 270) ? crop.width : crop.height;
    if (adjustedX < 0 || adjustedY < 0 || adjustedX >= outputWidth || adjustedY >= outputHeight)
        return result;

    int croppedX = adjustedX;
    int croppedY = adjustedY;
    switch (rotation)
    {
    case 90:
        croppedX = adjustedY;
        croppedY = crop.height - 1 - adjustedX;
        break;
    case 180:
        croppedX = crop.width - 1 - adjustedX;
        croppedY = crop.height - 1 - adjustedY;
        break;
    case 270:
        croppedX = crop.width - 1 - adjustedY;
        croppedY = adjustedX;
        break;
    default:
        break;
    }

    const PixelRGBA sourcePixel = samplePixel(source, crop.x + croppedX, crop.y + croppedY);
    if (!sourcePixel.valid)
        return result;

    result.r = adjustAnalysisChannel(sourcePixel.r, adjustment);
    result.g = adjustAnalysisChannel(sourcePixel.g, adjustment);
    result.b = adjustAnalysisChannel(sourcePixel.b, adjustment);
    if (source.format != PixelFormat::Grayscale8)
    {
        if (std::abs(static_cast<float>(adjustment.redGain) - 1.0f) >= 1e-6f)
            result.r = std::clamp(static_cast<int>(std::lroundf(
                                     static_cast<float>(result.r) *
                                     std::max(static_cast<float>(adjustment.redGain), 0.01f))),
                                  0, 255);
        if (std::abs(static_cast<float>(adjustment.blueGain) - 1.0f) >= 1e-6f)
            result.b = std::clamp(static_cast<int>(std::lroundf(
                                     static_cast<float>(result.b) *
                                     std::max(static_cast<float>(adjustment.blueGain), 0.01f))),
                                  0, 255);
    }
    result.valid = true;
    return result;
}

NeighborhoodStats neighborhoodStats(const ImageData &source, const AnalysisAdjustment &adjustment,
                                    int adjustedX, int adjustedY, int n)
{
    NeighborhoodStats stats;
    if (source.isNull() || n < 1)
        return stats;

    long long sum = 0;
    long long sumSq = 0;
    long long rSum = 0;
    long long gSum = 0;
    long long bSum = 0;
    int minValue = 255;
    int maxValue = 0;
    const int half = n / 2;
    for (int dy = -half; dy <= half; ++dy)
    {
        for (int dx = -half; dx <= half; ++dx)
        {
            const AnalysisPixel pixel =
                sampleAnalysisPixel(source, adjustment, adjustedX + dx, adjustedY + dy);
            if (!pixel.valid)
                continue;
            const int luminance = static_cast<int>(0.2126 * pixel.r + 0.7152 * pixel.g +
                                                   0.0722 * pixel.b + 0.5);
            sum += luminance;
            sumSq += static_cast<long long>(luminance) * luminance;
            rSum += pixel.r;
            gSum += pixel.g;
            bSum += pixel.b;
            minValue = std::min(minValue, luminance);
            maxValue = std::max(maxValue, luminance);
            ++stats.count;
        }
    }
    if (stats.count == 0)
        return stats;

    stats.mean = static_cast<double>(sum) / stats.count;
    const double variance = static_cast<double>(sumSq) / stats.count - stats.mean * stats.mean;
    stats.variance = std::max(0.0, variance);
    stats.stdDev = std::sqrt(stats.variance);
    stats.min = minValue;
    stats.max = maxValue;
    stats.rMean = static_cast<double>(rSum) / stats.count;
    stats.gMean = static_cast<double>(gSum) / stats.count;
    stats.bMean = static_cast<double>(bSum) / stats.count;
    return stats;
}

NeighborhoodStats neighborhoodStats(const uint8_t *data, int stride, int width, int height, int cx,
                                    int cy, int n)
{
    NeighborhoodStats s;
    if (!data || width <= 0 || height <= 0 || n < 1 || cx < 0 || cy < 0 || cx >= width ||
        cy >= height)
        return s;

    long sum = 0, sumSq = 0;
    long rSum = 0, gSum = 0, bSum = 0;
    int mn = 255, mx = 0, count = 0;
    const int half = n / 2; // n=1→0, n=3→1, n=5→2, n=7→3
    for (int dy = -half; dy <= half; ++dy)
    {
        const int yy = cy + dy;
        if (yy < 0 || yy >= height)
            continue;
        const uint8_t *row = data + static_cast<size_t>(yy) * stride;
        for (int dx = -half; dx <= half; ++dx)
        {
            const int xx = cx + dx;
            if (xx < 0 || xx >= width)
                continue;
            const uint8_t *p = row + static_cast<size_t>(xx) * 3;
            rSum += p[0];
            gSum += p[1];
            bSum += p[2];
            const double lum = luma(p[0], p[1], p[2]); // 0..255
            const int v = static_cast<int>(lum + 0.5);
            sum += v;
            sumSq += static_cast<long>(v) * v;
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
            ++count;
        }
    }
    if (count == 0)
        return s;
    const double mean = static_cast<double>(sum) / count;
    const double var = static_cast<double>(sumSq) / count - mean * mean;
    s.mean = mean;
    s.variance = var > 0 ? var : 0.0;
    s.stdDev = std::sqrt(s.variance);
    s.min = mn;
    s.max = mx;
    s.count = count;
    s.rMean = static_cast<double>(rSum) / count;
    s.gMean = static_cast<double>(gSum) / count;
    s.bMean = static_cast<double>(bSum) / count;
    return s;
}

const char *colorSpaceLabel(ColorSpace space)
{
    switch (space)
    {
    case ColorSpace::RGB:
        return "RGB";
    case ColorSpace::HSV:
        return "HSV";
    case ColorSpace::Lab:
        return "Lab";
    case ColorSpace::YUV:
        return "YUV";
    case ColorSpace::YCbCr:
        return "YCbCr";
    case ColorSpace::XYZ:
        return "XYZ";
    case ColorSpace::HEX:
        return "HEX";
    }
    return "RGB";
}

std::string toHex(uint8_t r, uint8_t g, uint8_t b)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return std::string(buf);
}
} // namespace mviewer::core
