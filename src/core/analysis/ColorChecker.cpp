#include "core/analysis/ColorChecker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mviewer::core
{
namespace
{
} // namespace

const std::array<ColorCheckerPatch, kColorCheckerPatchCount> &colorCheckerReference()
{
    static const std::array<ColorCheckerPatch, kColorCheckerPatchCount> reference = {{
        {"Dark skin", {37.54, 14.37, 14.58}},     {"Light skin", {66.89, 19.13, 17.22}},
        {"Blue sky", {49.44, -4.23, -22.14}},     {"Foliage", {43.41, -14.56, 22.52}},
        {"Blue flower", {54.98, 8.19, -25.28}},   {"Bluish green", {70.65, -33.66, -0.42}},
        {"Orange", {62.32, 35.57, 57.58}},        {"Purplish blue", {40.21, 11.51, -50.19}},
        {"Moderate red", {49.98, 44.14, 14.02}},  {"Purple", {30.59, 22.93, -21.14}},
        {"Yellow green", {71.57, -24.18, 59.45}}, {"Orange yellow", {68.98, 18.86, 64.69}},
        {"Blue", {29.86, 13.04, -48.75}},         {"Green", {55.18, -39.34, 31.23}},
        {"Red", {42.29, 50.98, 26.16}},           {"Yellow", {81.45, 4.09, 79.32}},
        {"Magenta", {50.99, 50.69, -14.64}},      {"Cyan", {50.97, -29.36, -25.46}},
        {"White", {95.08, -1.00, 2.73}},          {"Neutral 8", {80.88, -0.58, 0.29}},
        {"Neutral 6.5", {66.68, -0.56, -0.18}},   {"Neutral 5", {51.21, -0.21, -0.20}},
        {"Neutral 3.5", {35.84, -0.27, -0.36}},   {"Black", {20.70, 0.02, -0.56}},
    }};
    return reference;
}

ColorCheckerResult measureColorChecker(const ImageData &image, int rectX, int rectY, int rectW,
                                       int rectH)
{
    ColorCheckerResult result;
    if (image.isNull() || rectW <= 0 || rectH <= 0)
        return result;

    const auto &reference = colorCheckerReference();
    // Small central box of each patch: robust to chart borders and gaps.
    const double cellW = static_cast<double>(rectW) / 6.0;
    const double cellH = static_cast<double>(rectH) / 4.0;
    const int sampleW = std::max(1, static_cast<int>(cellW * 0.3));
    const int sampleH = std::max(1, static_cast<int>(cellH * 0.3));

    for (std::size_t i = 0; i < kColorCheckerPatchCount; ++i)
    {
        const int row = static_cast<int>(i) / 6;
        const int col = static_cast<int>(i) % 6;
        const int cx = rectX + static_cast<int>((col + 0.5) * cellW);
        const int cy = rectY + static_cast<int>((row + 0.5) * cellH);

        long long sum[3] = {0, 0, 0};
        long long count = 0;
        for (int y = cy - sampleH / 2; y <= cy + sampleH / 2; ++y)
        {
            if (y < 0 || y >= image.height)
                continue;
            for (int x = cx - sampleW / 2; x <= cx + sampleW / 2; ++x)
            {
                if (x < 0 || x >= image.width)
                    continue;
                const PixelRGBA px = samplePixel(image, x, y);
                if (!px.valid)
                    continue;
                sum[0] += px.r;
                sum[1] += px.g;
                sum[2] += px.b;
                ++count;
            }
        }
        ColorCheckerSample &out = result.patches[i];
        if (count == 0)
            continue;
        const double r = static_cast<double>(sum[0]) / static_cast<double>(count) / 255.0;
        const double g = static_cast<double>(sum[1]) / static_cast<double>(count) / 255.0;
        const double b = static_cast<double>(sum[2]) / static_cast<double>(count) / 255.0;
        // sRGB -> linear -> XYZ -> Lab (D65).
        const auto toLinear = [](double v)
        { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        const double lr = toLinear(r);
        const double lg = toLinear(g);
        const double lb = toLinear(b);
        // sRGB (D65) -> CIE XYZ -> Lab.
        const Xyz xyz{0.4124564 * lr + 0.3575761 * lg + 0.1804375 * lb,
                      0.2126729 * lr + 0.7151522 * lg + 0.0721750 * lb,
                      0.0193339 * lr + 0.1191920 * lg + 0.9503041 * lb};
        out.measured = labFromXyz(xyz);
        out.deltaE = ciede2000(reference[i].reference, out.measured);
        out.valid = true;
        ++result.validCount;
        result.meanDeltaE += out.deltaE;
        result.maxDeltaE = std::max(result.maxDeltaE, out.deltaE);
    }
    if (result.validCount > 0)
        result.meanDeltaE /= static_cast<double>(result.validCount);
    return result;
}

std::string colorCheckerReportText(const ColorCheckerResult &result)
{
    std::string out;
    for (std::size_t i = 0; i < kColorCheckerPatchCount; ++i)
    {
        const ColorCheckerSample &s = result.patches[i];
        char line[160];
        if (s.valid)
            std::snprintf(line, sizeof(line), "%-14s dE00 %6.2f  (L %6.2f a %7.2f b %7.2f)\n",
                          colorCheckerReference()[i].name, s.deltaE, s.measured.L, s.measured.a,
                          s.measured.b);
        else
            std::snprintf(line, sizeof(line), "%-14s (out of bounds)\n",
                          colorCheckerReference()[i].name);
        out += line;
    }
    char summary[96];
    std::snprintf(summary, sizeof(summary), "Mean dE00 %.2f   Max %.2f   Valid %d/24\n",
                  result.meanDeltaE, result.maxDeltaE, result.validCount);
    out += summary;
    return out;
}

} // namespace mviewer::core
