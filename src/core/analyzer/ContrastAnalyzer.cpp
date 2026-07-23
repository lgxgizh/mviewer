#include "core/analyzer/ContrastAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

bool ContrastAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n <= 0)
        return false;

    double sum = 0, sum2 = 0;
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            double l;
            if (cpp >= 3)
                l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
            else
                l = p[0];
            sum += l;
            sum2 += l * l;
        }
    }
    m_result.mean = sum / n;
    m_result.rms = std::sqrt(sum2 / n - m_result.mean * m_result.mean);
    m_result.ok = true;
    return true;
}

bool ContrastAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool ContrastAnalyzer::analyzeRegion(const ImageFrame &frame,
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

std::string ContrastAnalyzer::resultText() const
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), "RMS contrast: %.2f  mean lum: %.1f", m_result.rms,
                  m_result.mean);
    return buf;
}

std::unordered_map<std::string, double> ContrastAnalyzer::resultMetrics() const
{
    return {{"rmsContrast", m_result.rms}, {"meanLuminance", m_result.mean}};
}
