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

    double sum = 0, sum2 = 0;
    for (int y = y0 + 1; y < y1 - 1; ++y)
    {
        const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
        const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
        const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
        for (int x = x0 + 1; x < x1 - 1; ++x)
        {
            auto lum = [&](int ox, int oy) -> double
            {
                const uint8_t *line =
                    v.data + static_cast<size_t>(y + oy) * v.stride();
                const uint8_t *p = line + static_cast<size_t>(x + ox) * cpp;
                if (cpp >= 3)
                    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
                return p[0];
            };
            // Laplacian 3x3
            double lap = 0;
            lap += 1.0 * lum(0, -1);
            lap += 1.0 * lum(-1, 0);
            lap += -4.0 * lum(0, 0);
            lap += 1.0 * lum(1, 0);
            lap += 1.0 * lum(0, 1);
            sum2 += lap * lap;
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

bool BlurAnalyzer::analyzeRegion(const ImageFrame &frame,
                                 const mviewer::domain::Selection &region)
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
