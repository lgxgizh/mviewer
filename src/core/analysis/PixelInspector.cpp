#include "core/analysis/PixelInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// NOLINTBEGIN(bugprone-easily-swappable-parameters,bugprone-incorrect-roundings,cppcoreguidelines-pro-type-vararg)

namespace mviewer::core
{
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

// Precomputed 256-entry lookup table for exact sRGB (0..255) to Linear RGB conversion.
// Eliminates transcendental std::pow calls in high-frequency pixel inspection (XYZ / Lab).
const std::array<double, 256> &srgbToLinearTable()
{
    static const auto table = []()
    {
        std::array<double, 256> lut{};
        for (int i = 0; i < 256; ++i)
        {
            const double c = i / 255.0;
            lut[static_cast<size_t>(i)] =
                (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
        }
        return lut;
    }();
    return table;
}
} // namespace

ColorTriple toColorSpace(uint8_t r, uint8_t g, uint8_t b, ColorSpace space)
{
    if (space == ColorSpace::RGB || space == ColorSpace::HEX)
        return {double(r), double(g), double(b)};
    if (space == ColorSpace::XYZ)
    {
        const auto &lut = srgbToLinearTable();
        const double lr = lut[r], lg = lut[g], lb = lut[b];
        return {lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375,
                lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750,
                lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041};
    }
    if (space == ColorSpace::Lab)
    {
        const auto &lut = srgbToLinearTable();
        const double lr = lut[r], lg = lut[g], lb = lut[b];
        const double X = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / 0.95047;
        const double Y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / 1.00000;
        const double Z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / 1.08883;
        auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0); };
        const double fx = f(X), fy = f(Y), fz = f(Z);
        return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
    }
    if (space == ColorSpace::YUV)
    {
        const double Y = 0.299 * r + 0.587 * g + 0.114 * b;
        return {Y, 0.564 * (b - Y), 0.713 * (r - Y)};
    }
    if (space == ColorSpace::YCbCr)
    {
        return {0.299 * r + 0.587 * g + 0.114 * b, -0.168736 * r - 0.331264 * g + 0.5 * b + 128.0,
                0.5 * r - 0.418688 * g - 0.081312 * b + 128.0};
    }
    if (space == ColorSpace::HSV)
    {
        const int mx = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
        const int mn = std::min({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
        const int d = mx - mn;
        double h = 0.0;
        if (d > 0)
        {
            if (mx == r)
                h = std::fmod(
                    60.0 * (static_cast<double>(static_cast<int>(g) - static_cast<int>(b)) / d),
                    360.0);
            else if (mx == g)
                h = 60.0 *
                    (static_cast<double>(static_cast<int>(b) - static_cast<int>(r)) / d + 2.0);
            else
                h = 60.0 *
                    (static_cast<double>(static_cast<int>(r) - static_cast<int>(g)) / d + 4.0);
            if (h < 0.0)
                h += 360.0;
        }
        const double v = (mx * 100.0) / 255.0;
        const double s = mx > 0 ? (static_cast<double>(d) / mx) * 100.0 : 0.0;
        return {h, s, v};
    }
    return toColorSpaceNorm(r / 255.0, g / 255.0, b / 255.0, space);
}

ColorTriple toColorSpace(uint16_t r, uint16_t g, uint16_t b, uint16_t maxVal, ColorSpace space)
{
    if (maxVal == 0)
        maxVal = 1;
    if (maxVal == 255)
        return toColorSpace(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                            static_cast<uint8_t>(b), space);
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

// NOLINTEND(bugprone-easily-swappable-parameters,bugprone-incorrect-roundings,cppcoreguidelines-pro-type-vararg)
