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
    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    if (isGray)
    {
        int64_t iSum = 0;
        int64_t iSum2 = 0;
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = x0; x < x1; ++x)
            {
                const uint8_t val = line[x];
                iSum += val;
                iSum2 += static_cast<int64_t>(val) * val;
            }
        }
        sum = static_cast<double>(iSum);
        sum2 = static_cast<double>(iSum2);
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
                sum2 += l * l;
            }
        }
    }
    m_result.mean = sum / n;
    m_result.rms = std::sqrt(std::max(0.0, sum2 / n - m_result.mean * m_result.mean));
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
