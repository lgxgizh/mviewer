#include "core/analyzer/ExposureAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cstdio>

bool ExposureAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);

    int64_t shadows = 0, highlights = 0;
    int64_t iSum = 0;
    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    if (isGray)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const uint8_t val = line[x];
                iSum += val;
                if (val < 25)
                    ++shadows;
                else if (val > 230)
                    ++highlights;
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
                const int r = isBGR ? p[2] : p[0];
                const int g = p[1];
                const int b = isBGR ? p[0] : p[2];
                const int l = (19595 * r + 38470 * g + 7471 * b) >> 16;
                iSum += l;
                if (l < 25)
                    ++shadows;
                else if (l > 230)
                    ++highlights;
            }
        }
    }
    m_result.shadowPct = (static_cast<double>(shadows) / n) * 100.0;
    m_result.highlightPct = (static_cast<double>(highlights) / n) * 100.0;
    m_result.avgLum = static_cast<double>(iSum) / n;
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
