#include "core/analyzer/NoiseAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

#include <cmath>
#include <unordered_map>

namespace
{
inline int getAvgInt(const uint8_t *p)
{
    return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
}
} // namespace

// Laplacian variance: |Laplacian(img)| variance, where L = [0 1 0; 1 -4 1; 0 1
// 0].
double NoiseAnalyzer::estimateLaplacian(const ImageBuffer &v, int x0, int y0, int x1, int y1) const
{
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    if (x0 < 1)
        x0 = 1;
    if (y0 < 1)
        y0 = 1;
    if (x1 >= w)
        x1 = w - 1;
    if (y1 >= h)
        y1 = h - 1;
    if (x1 <= x0 || y1 <= y0)
        return 0.0;
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    double sum = 0, sum2 = 0;
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    if (isGray)
    {
        int64_t iSum = 0;
        int64_t iSum2 = 0;
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const int c = static_cast<int>(line1[x]) * 4;
                const int n4 = static_cast<int>(line0[x]) + line2[x] + line1[x - 1] + line1[x + 1];
                const int lap = c - n4;
                iSum += lap;
                iSum2 += static_cast<int64_t>(lap) * lap;
            }
        }
        sum = static_cast<double>(iSum);
        sum2 = static_cast<double>(iSum2);
    }
    else
    {
        int64_t iSum = 0;
        int64_t iSum2 = 0;
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            const uint8_t *p0 = line0 + static_cast<size_t>(x0) * cpp;
            const uint8_t *p1 = line1 + static_cast<size_t>(x0) * cpp;
            const uint8_t *p2 = line2 + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p0 += cpp, p1 += cpp, p2 += cpp)
            {
                const int c = getAvgInt(p1) * 4;
                const int n4 =
                    getAvgInt(p0) + getAvgInt(p2) + getAvgInt(p1 - cpp) + getAvgInt(p1 + cpp);
                const int lap = c - n4;
                iSum += lap;
                iSum2 += static_cast<int64_t>(lap) * lap;
            }
        }
        sum = static_cast<double>(iSum);
        sum2 = static_cast<double>(iSum2);
    }
    const double mean = sum / n;
    return std::max(0.0, sum2 / n - mean * mean);
}

bool NoiseAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    m_noise = estimateLaplacian(v, 0, 0, v.width, v.height);
    return true;
}

bool NoiseAnalyzer::analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region)
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
    if (x1 <= x0 || y1 <= y0)
        return false;
    m_noise = estimateLaplacian(v, x0, y0, x1, y1);
    return true;
}

std::string NoiseAnalyzer::resultText() const
{
    return "noise (Laplacian variance): " + std::to_string(m_noise);
}

std::unordered_map<std::string, double> NoiseAnalyzer::resultMetrics() const
{
    // m_noise is the Laplacian-response variance (the analyzer's primary
    // scalar). We also emit its std (sqrt) for convenience. There is no
    // separate "noise mean" stored by the analyzer.
    return {{"noiseVariance", m_noise}, {"noiseStd", m_noise > 0.0 ? std::sqrt(m_noise) : 0.0}};
}
