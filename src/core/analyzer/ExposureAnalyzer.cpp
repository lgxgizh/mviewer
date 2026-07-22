#include "core/analyzer/ExposureAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cstdio>

bool ExposureAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n <= 0)
        return false;

    int64_t shadows = 0, highlights = 0;
    double sumLum = 0;
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
            sumLum += l;
            if (l < 25.0)
                ++shadows;
            else if (l > 230.0)
                ++highlights;
        }
    }
    m_result.shadowPct = (static_cast<double>(shadows) / n) * 100.0;
    m_result.highlightPct = (static_cast<double>(highlights) / n) * 100.0;
    m_result.avgLum = sumLum / n;
    m_result.ok = true;
    return true;
}

bool ExposureAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool ExposureAnalyzer::analyzeRegion(const ImageFrame &frame,
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

std::string ExposureAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Shadows: %.1f%%  Highlights: %.1f%%  Avg: %.1f",
                  m_result.shadowPct, m_result.highlightPct, m_result.avgLum);
    return buf;
}

std::unordered_map<std::string, double> ExposureAnalyzer::resultMetrics() const
{
    return {{"shadowPct", m_result.shadowPct},
            {"highlightPct", m_result.highlightPct},
            {"avgLuminance", m_result.avgLum}};
}
