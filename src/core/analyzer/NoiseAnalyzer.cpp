#include "core/analyzer/NoiseAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

#include <cmath>
#include <unordered_map>

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
        auto getAvg = [cpp](const uint8_t *line, int x) -> double
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            return (p[0] + p[1] + p[2]) / 3.0;
        };

        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const double c = getAvg(line1, x) * 4.0;
                const double n4 = getAvg(line0, x) + getAvg(line2, x) + getAvg(line1, x - 1) +
                                  getAvg(line1, x + 1);
                const double lap = c - n4;
                sum += lap;
                sum2 += lap * lap;
            }
        }
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
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
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
