#include "core/analyzer/BrightnessAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cstdio>

bool BrightnessAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n <= 0)
        return false;

    double sum = 0;
    double mn = 255, mx = 0;
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            if (cpp >= 3)
            {
                const double l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
                sum += l;
                mn = std::min(mn, l);
                mx = std::max(mx, l);
            }
            else
            {
                sum += p[0];
                mn = std::min(mn, static_cast<double>(p[0]));
                mx = std::max(mx, static_cast<double>(p[0]));
            }
        }
    }
    m_result.avgLum = sum / n;
    m_result.minLum = mn;
    m_result.maxLum = mx;
    m_result.ok = true;
    return true;
}

bool BrightnessAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    return compute(frame.pixels().view(), 0, 0, frame.pixels().view().width,
                   frame.pixels().view().height);
}

bool BrightnessAnalyzer::analyzeRegion(const ImageFrame &frame,
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

std::string BrightnessAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Lum: avg=%.1f  min=%.0f  max=%.0f", m_result.avgLum,
                  m_result.minLum, m_result.maxLum);
    return buf;
}

std::unordered_map<std::string, double> BrightnessAnalyzer::resultMetrics() const
{
    return {{"avgLuminance", m_result.avgLum},
            {"minLuminance", m_result.minLum},
            {"maxLuminance", m_result.maxLum}};
}
