#include "core/analyzer/ColorCastAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

bool ColorCastAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    const int cpp = v.channelsPerPixel();
    if (cpp < 3)
        return false; // grayscale has no color cast
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n <= 0)
        return false;

    double sumR = 0, sumG = 0, sumB = 0;
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            sumR += p[0];
            sumG += p[1];
            sumB += p[2];
        }
    }
    const double meanR = sumR / n;
    const double meanG = sumG / n;
    const double meanB = sumB / n;
    const double gray = (meanR + meanG + meanB) / 3.0;

    m_result.castR = meanR - gray;
    m_result.castG = meanG - gray;
    m_result.castB = meanB - gray;
    m_result.magnitude =
        std::sqrt(m_result.castR * m_result.castR + m_result.castG * m_result.castG +
                  m_result.castB * m_result.castB);
    m_result.ok = true;
    return true;
}

bool ColorCastAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool ColorCastAnalyzer::analyzeRegion(const ImageFrame &frame,
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

std::string ColorCastAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Color cast: R%+.1f G%+.1f B%+.1f  mag=%.1f", m_result.castR,
                  m_result.castG, m_result.castB, m_result.magnitude);
    return buf;
}

std::unordered_map<std::string, double> ColorCastAnalyzer::resultMetrics() const
{
    return {{"castR", m_result.castR},
            {"castG", m_result.castG},
            {"castB", m_result.castB},
            {"castMagnitude", m_result.magnitude}};
}
