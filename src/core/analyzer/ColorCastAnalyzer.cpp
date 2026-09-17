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
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);

    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    int64_t sumR = 0, sumG = 0, sumB = 0;

    if (isBGR)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *p = line + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                sumB += p[0];
                sumG += p[1];
                sumR += p[2];
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
                sumR += p[0];
                sumG += p[1];
                sumB += p[2];
            }
        }
    }

    const double meanR = static_cast<double>(sumR) / n;
    const double meanG = static_cast<double>(sumG) / n;
    const double meanB = static_cast<double>(sumB) / n;
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
