#include "core/analyzer/BlurAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// Laplacian kernel (3x3): [[0,1,0],[1,-4,1],[0,1,0]] applied to luminance.
// Returns variance of the response as blur metric.
bool BlurAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    const int cpp = v.channelsPerPixel();
    if (x1 - x0 < 3 || y1 - y0 < 3)
        return false;
    const int64_t n = static_cast<int64_t>(x1 - x0 - 2) * (y1 - y0 - 2);
    if (n <= 0)
        return false;

    double sum2 = 0;
    const bool isGray = (v.format == PixelFormat::Grayscale8);
    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);

    if (isGray)
    {
        int64_t iSum2 = 0;
        for (int y = y0 + 1; y < y1 - 1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0 + 1; x < x1 - 1; ++x)
            {
                const int lap = static_cast<int>(line0[x]) + line1[x - 1] + line1[x + 1] +
                                line2[x] - 4 * static_cast<int>(line1[x]);
                iSum2 += static_cast<int64_t>(lap) * lap;
            }
        }
        sum2 = static_cast<double>(iSum2);
    }
    else
    {
        auto getLum = [cpp, isBGR](const uint8_t *line, int x) -> double
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            const double r = isBGR ? p[2] : p[0];
            const double g = p[1];
            const double b = isBGR ? p[0] : p[2];
            return 0.299 * r + 0.587 * g + 0.114 * b;
        };

        for (int y = y0 + 1; y < y1 - 1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0 + 1; x < x1 - 1; ++x)
            {
                const double lap = getLum(line0, x) + getLum(line1, x - 1) + getLum(line1, x + 1) +
                                   getLum(line2, x) - 4.0 * getLum(line1, x);
                sum2 += lap * lap;
            }
        }
    }
    m_result.variance = sum2 / n;
    m_result.ok = true;
    return true;
}

bool BlurAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool BlurAnalyzer::analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;
    const ImageBuffer v = frame.pixels().view();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
    return compute(v, x0, y0, x1, y1);
}

std::string BlurAnalyzer::resultText() const
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), "Laplacian variance: %.2f (%.0f = sharp, <100 = blurry)",
                  m_result.variance, m_result.variance);
    return buf;
}

std::unordered_map<std::string, double> BlurAnalyzer::resultMetrics() const
{
    return {{"laplacianVariance", m_result.variance}};
}
