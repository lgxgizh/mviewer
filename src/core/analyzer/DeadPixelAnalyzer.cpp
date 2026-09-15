#include "core/analyzer/DeadPixelAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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
                int neigh[8] = {
                    line0[x - 1], line0[x], line0[x + 1],
                    line1[x - 1],           line1[x + 1],
                    line2[x - 1], line2[x], line2[x + 1]
                };
                std::nth_element(neigh, neigh + 4, neigh + 8);
                const int med = neigh[4];
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
        auto getAvg = [cpp](const uint8_t *line, int x) -> int
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
        };

        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line0 = v.data + static_cast<size_t>(y - 1) * v.stride();
            const uint8_t *line1 = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *line2 = v.data + static_cast<size_t>(y + 1) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const int lum = getAvg(line1, x);
                int neigh[8] = {
                    getAvg(line0, x - 1), getAvg(line0, x), getAvg(line0, x + 1),
                    getAvg(line1, x - 1),                   getAvg(line1, x + 1),
                    getAvg(line2, x - 1), getAvg(line2, x), getAvg(line2, x + 1)
                };
                std::nth_element(neigh, neigh + 4, neigh + 8);
                const int med = neigh[4];
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
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
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
