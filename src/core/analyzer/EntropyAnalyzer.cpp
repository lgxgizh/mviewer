#include "core/analyzer/EntropyAnalyzer.h"

#include <cmath>
#include <cstring>

double EntropyAnalyzer::computeEntropy(const ImageBuffer &v, int x0, int y0, int x1, int y1) const
{
    if (x1 <= x0 || y1 <= y0)
        return 0.0;
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    int hist[256] = {0};
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n == 0)
        return 0.0;
    const bool isGray = (v.format == PixelFormat::Grayscale8);
    if (isGray)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                ++hist[line[x]];
            }
        }
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *p = line + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                const int sum = static_cast<int>(p[0]) + p[1] + p[2];
                const int lum = (sum * 21846) >> 16;
                ++hist[lum];
            }
        }
    }
    double H = 0.0;
    for (int i = 0; i < 256; ++i)
    {
        if (hist[i] == 0)
            continue;
        const double p = static_cast<double>(hist[i]) / n;
        H -= p * std::log2(p);
    }
    return H;
}

bool EntropyAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    m_entropy = computeEntropy(v, 0, 0, v.width, v.height);
    return true;
}

bool EntropyAnalyzer::analyzeRegion(const ImageFrame &frame,
                                    const mviewer::domain::Selection &region)
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
    m_entropy = computeEntropy(v, x0, y0, x1, y1);
    return true;
}

std::string EntropyAnalyzer::resultText() const
{
    return "entropy (bits/px): " + std::to_string(m_entropy);
}

std::unordered_map<std::string, double> EntropyAnalyzer::resultMetrics() const
{
    return {{"entropy", m_entropy}};
}
