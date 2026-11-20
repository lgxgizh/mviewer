// ColorChecker measurement (report feature 6). The fixture is rendered FROM
// the reference Lab values, so a mean dE00 of ~0 proves the whole chain
// (Lab -> linear RGB -> sRGB -> XYZ -> Lab -> CIEDE2000 -> grid sampling)
// rather than the accuracy of any particular published table.
#include "core/analysis/ColorChecker.h"

#include <cmath>
#include <cstdio>
#include <string>

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

constexpr double kRefX = 0.95047;
constexpr double kRefY = 1.00000;
constexpr double kRefZ = 1.08883;
constexpr double kEpsilon = 216.0 / 24389.0;

double inverseLabF(double t)
{
    const double delta = 6.0 / 29.0;
    return t > delta ? t * t * t : 3.0 * delta * delta * (t - 4.0 / 29.0);
}

int linearToSrgb8(double v)
{
    if (v <= 0.0)
        return 0;
    if (v >= 1.0)
        return 255;
    const double encoded = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    return static_cast<int>(std::lround(encoded * 255.0));
}
} // namespace

int main()
{
    using mviewer::core::colorCheckerReference;
    const auto &reference = colorCheckerReference();
    CHECK(reference.size() == 24, "the reference chart has 24 patches");

    // Render the chart at 6 x 4 patches of 20x20 pixels.
    const int cell = 20;
    const int width = 6 * cell;
    const int height = 4 * cell;
    ImageData chart = makeImageData(width, height, PixelFormat::RGB24);
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 6; ++col)
        {
            const auto &patch = reference[static_cast<size_t>(row * 6 + col)];
            const double fy = (patch.reference.L + 16.0) / 116.0;
            const double fx = fy + patch.reference.a / 500.0;
            const double fz = fy - patch.reference.b / 200.0;
            // XYZ from Lab (D65), then the inverse sRGB D65 matrix.
            const double X = inverseLabF(fx) * kRefX;
            const double Y = inverseLabF(fy) * kRefY;
            const double Z = inverseLabF(fz) * kRefZ;
            const double linR = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
            const double linG = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
            const double linB = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
            const int r = linearToSrgb8(linR);
            const int g = linearToSrgb8(linG);
            const int b = linearToSrgb8(linB);
            for (int y = row * cell; y < (row + 1) * cell; ++y)
                for (int x = col * cell; x < (col + 1) * cell; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) +
                                      static_cast<size_t>(x)) *
                                     3u;
                    chart.mutableView().data[i + 0u] = static_cast<std::uint8_t>(r);
                    chart.mutableView().data[i + 1u] = static_cast<std::uint8_t>(g);
                    chart.mutableView().data[i + 2u] = static_cast<std::uint8_t>(b);
                }
        }
    }

    const mviewer::core::ColorCheckerResult result =
        mviewer::core::measureColorChecker(chart, 0, 0, width, height);
    CHECK(result.validCount == 24, "all 24 patches are measured");
    CHECK(result.meanDeltaE < 1.0, "a chart rendered from the references measures mean dE00 < 1");
    CHECK(result.maxDeltaE < 6.0, "no patch is far off (8-bit quantization on saturated patches)");

    // A rectangle entirely outside the image yields no patches rather than
    // silently sampling the wrong pixels.
    const mviewer::core::ColorCheckerResult outside =
        mviewer::core::measureColorChecker(chart, width + 50, height + 50, 60, 40);
    CHECK(outside.validCount == 0, "a rectangle outside the image measures nothing");

    // A degenerate rectangle yields nothing instead of dividing by zero.
    const mviewer::core::ColorCheckerResult empty =
        mviewer::core::measureColorChecker(chart, 0, 0, 0, 0);
    CHECK(empty.validCount == 0 && empty.meanDeltaE == 0.0, "an empty rectangle is inert");

    const std::string text = mviewer::core::colorCheckerReportText(result);
    CHECK(text.find("Mean dE00") != std::string::npos &&
              text.find("Dark skin") != std::string::npos,
          "the report text is human-readable");

    printf("\n==== color checker test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
