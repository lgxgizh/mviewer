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
    double mn = 255.0, mx = 0.0;
    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    if (isGray)
    {
        int64_t iSum = 0;
        uint8_t iMn = 255, iMx = 0;
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const uint8_t val = line[x];
                iSum += val;
                iMn = std::min(iMn, val);
                iMx = std::max(iMx, val);
            }
        }
        sum = static_cast<double>(iSum);
        mn = static_cast<double>(iMn);
        mx = static_cast<double>(iMx);
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const uint8_t *p = line + static_cast<size_t>(x) * cpp;
                const double r = isBGR ? p[2] : p[0];
                const double g = p[1];
                const double b = isBGR ? p[0] : p[2];
                const double l = 0.299 * r + 0.587 * g + 0.114 * b;
                sum += l;
                mn = std::min(mn, l);
                mx = std::max(mx, l);
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
