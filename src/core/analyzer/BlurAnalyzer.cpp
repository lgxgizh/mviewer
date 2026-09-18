#include "core/analyzer/BlurAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
inline int getLumInt(const uint8_t *p, bool isBGR)
{
    const int r = isBGR ? p[2] : p[0];
    const int g = p[1];
    const int b = isBGR ? p[0] : p[2];
    return (19595 * r + 38470 * g + 7471 * b) >> 16;
}
} // namespace

// Laplacian kernel (3x3): [[0,1,0],[1,-4,1],[0,1,0]] applied to luminance.
// Returns variance of the response as blur metric.
bool BlurAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x1 - x0 < 3 || y1 - y0 < 3)
        return false;
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0 - 2) * (y1 - y0 - 2);

    int64_t iSum2 = 0;
    const bool isGray = (v.format == PixelFormat::Grayscale8);
    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);

    if (isGray)
    {
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
    }
    else
    {
        for (int y = y0 + 1; y < y1 - 1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            const uint8_t *p0 = line0 + static_cast<size_t>(x0 + 1) * cpp;
            const uint8_t *p1 = line1 + static_cast<size_t>(x0 + 1) * cpp;
            const uint8_t *p2 = line2 + static_cast<size_t>(x0 + 1) * cpp;
            for (int x = x0 + 1; x < x1 - 1; ++x, p0 += cpp, p1 += cpp, p2 += cpp)
            {
                const int lap = getLumInt(p0, isBGR) + getLumInt(p1 - cpp, isBGR) +
                                getLumInt(p1 + cpp, isBGR) + getLumInt(p2, isBGR) -
                                4 * getLumInt(p1, isBGR);
                iSum2 += static_cast<int64_t>(lap) * lap;
            }
        }
    }
    // Laplacian energy fits in double mantissa for ROI sizes we support.
    // NOLINTBEGIN(bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions)
    m_result.variance = static_cast<double>(iSum2) / static_cast<double>(n);
    // NOLINTEND(bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions)
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
    const long long x0ll = std::clamp<long long>(region.x, 0, v.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, v.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, v.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, v.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 - x0 < 3 || y1 - y0 < 3)
        return false;
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
