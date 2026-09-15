#include "core/analyzer/SharpnessAnalyzer.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

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
                const double gx =
                    (getAvg(line0, x + 1) + 2.0 * getAvg(line1, x + 1) + getAvg(line2, x + 1)) -
                    (getAvg(line0, x - 1) + 2.0 * getAvg(line1, x - 1) + getAvg(line2, x - 1));
                const double gy =
                    (getAvg(line2, x - 1) + 2.0 * getAvg(line2, x) + getAvg(line2, x + 1)) -
                    (getAvg(line0, x - 1) + 2.0 * getAvg(line0, x) + getAvg(line0, x + 1));
                sumG += std::sqrt(gx * gx + gy * gy);
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
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
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
