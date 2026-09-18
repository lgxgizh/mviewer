#include "core/analyzer/SharpnessAnalyzer.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace
{
inline int getAvgInt(const uint8_t *p)
{
    return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
}
} // namespace

double SharpnessAnalyzer::computeSharpness(const ImageBuffer &v, int x0, int y0, int x1,
                                           int y1) const
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
    double sumG = 0.0;
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    if (isGray)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const int gx = (static_cast<int>(line0[x + 1]) + 2 * line1[x + 1] + line2[x + 1]) -
                               (static_cast<int>(line0[x - 1]) + 2 * line1[x - 1] + line2[x - 1]);
                const int gy = (static_cast<int>(line2[x - 1]) + 2 * line2[x] + line2[x + 1]) -
                               (static_cast<int>(line0[x - 1]) + 2 * line0[x] + line0[x + 1]);
                sumG += std::sqrt(static_cast<double>(gx * gx + gy * gy));
            }
        }
    }
    else
    {
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
                const int gx =
                    (getAvgInt(p0 + cpp) + 2 * getAvgInt(p1 + cpp) + getAvgInt(p2 + cpp)) -
                    (getAvgInt(p0 - cpp) + 2 * getAvgInt(p1 - cpp) + getAvgInt(p2 - cpp));
                const int gy = (getAvgInt(p2 - cpp) + 2 * getAvgInt(p2) + getAvgInt(p2 + cpp)) -
                               (getAvgInt(p0 - cpp) + 2 * getAvgInt(p0) + getAvgInt(p0 + cpp));
                sumG += std::sqrt(static_cast<double>(gx * gx + gy * gy));
            }
        }
    }
    return sumG / n;
}

bool SharpnessAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    m_sharp = computeSharpness(v, 0, 0, v.width, v.height);
    return true;
}

bool SharpnessAnalyzer::analyzeRegion(const ImageFrame &frame,
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
    m_sharp = computeSharpness(v, x0, y0, x1, y1);
    return true;
}

std::string SharpnessAnalyzer::resultText() const
{
    return "sharpness (gradient magnitude): " + std::to_string(m_sharp);
}

std::unordered_map<std::string, double> SharpnessAnalyzer::resultMetrics() const
{
    return {{"sharpness", m_sharp}};
}
