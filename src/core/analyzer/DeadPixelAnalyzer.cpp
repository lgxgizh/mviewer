#include "core/analyzer/DeadPixelAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace
{
inline void swapIfLess(int &a, int &b)
{
    if (b < a)
        std::swap(a, b);
}

inline int median8(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7)
{
    swapIfLess(a0, a1);
    swapIfLess(a2, a3);
    swapIfLess(a4, a5);
    swapIfLess(a6, a7);
    swapIfLess(a0, a2);
    swapIfLess(a1, a3);
    swapIfLess(a4, a6);
    swapIfLess(a5, a7);
    swapIfLess(a1, a2);
    swapIfLess(a5, a6);
    swapIfLess(a0, a4);
    swapIfLess(a1, a5);
    swapIfLess(a2, a6);
    swapIfLess(a3, a7);
    swapIfLess(a2, a4);
    swapIfLess(a3, a5);
    swapIfLess(a1, a2);
    swapIfLess(a3, a4);
    swapIfLess(a5, a6);
    return a4;
}

inline int getAvgInt(const uint8_t *p)
{
    return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
}
} // namespace

bool DeadPixelAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x0 < 1)
        x0 = 1;
    if (y0 < 1)
        y0 = 1;
    if (x1 > v.width - 1)
        x1 = v.width - 1;
    if (y1 > v.height - 1)
        y1 = v.height - 1;
    const int cpp = v.channelsPerPixel();
    if (x1 <= x0 || y1 <= y0)
    {
        m_count = 0;
        m_maxDev = 0;
        return false;
    }

    m_count = 0;
    m_maxDev = 0;
    const int kThresh = 40; // luminance deviation (0..255) to flag a dead pixel
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
                const int lum = line1[x];
                const int med = median8(line0[x - 1], line0[x], line0[x + 1], line1[x - 1],
                                        line1[x + 1], line2[x - 1], line2[x], line2[x + 1]);
                const int dev = std::abs(lum - med);
                if (dev > kThresh)
                {
                    ++m_count;
                    m_maxDev = std::max(m_maxDev, dev);
                }
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
                const int lum = getAvgInt(p1);
                const int med = median8(getAvgInt(p0 - cpp), getAvgInt(p0), getAvgInt(p0 + cpp),
                                        getAvgInt(p1 - cpp), getAvgInt(p1 + cpp),
                                        getAvgInt(p2 - cpp), getAvgInt(p2), getAvgInt(p2 + cpp));
                const int dev = std::abs(lum - med);
                if (dev > kThresh)
                {
                    ++m_count;
                    m_maxDev = std::max(m_maxDev, dev);
                }
            }
        }
    }
    return true;
}

bool DeadPixelAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool DeadPixelAnalyzer::analyzeRegion(const ImageFrame &frame,
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
    return compute(v, x0, y0, x1, y1);
}

std::string DeadPixelAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "dead/hot pixels: %d (max dev %d)", m_count, m_maxDev);
    return std::string(buf);
}

std::unordered_map<std::string, double> DeadPixelAnalyzer::resultMetrics() const
{
    return {{"deadPixelCount", static_cast<double>(m_count)},
            {"maxDeviation", static_cast<double>(m_maxDev)}};
}
