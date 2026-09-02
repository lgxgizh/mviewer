#include "core/analyzer/RGBMeanAnalyzer.h"

#include "core/image/ImageStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
bool computeRGB(const ImageData &img, const mviewer::domain::Selection &region,
                RGBMeanAnalyzer::Result &out)
{
    out = {};
    const auto measured = mviewer::core::computeROIChannelStats(img, region);
    if (!measured.valid)
        return false;

    const long long x0ll = std::clamp<long long>(region.x, 0, img.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, img.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, img.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, img.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return false;

    long double sumR2 = 0.0L;
    long double sumG2 = 0.0L;
    long double sumB2 = 0.0L;
    const ImageBuffer view = img.view();
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * view.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * view.channelsPerPixel();
            uint8_t r = 0, g = 0, b = 0;
            switch (view.format)
            {
            case PixelFormat::BGR24:
            case PixelFormat::BGRA32:
                b = p[0];
                g = p[1];
                r = p[2];
                break;
            case PixelFormat::Grayscale8:
                r = g = b = p[0];
                break;
            case PixelFormat::RGB24:
            case PixelFormat::RGBA32:
            default:
                r = p[0];
                g = p[1];
                b = p[2];
                break;
            }
            sumR2 += static_cast<long double>(r) * r;
            sumG2 += static_cast<long double>(g) * g;
            sumB2 += static_cast<long double>(b) * b;
        }
    }

    out.rMean = measured.rMean;
    out.gMean = measured.gMean;
    out.bMean = measured.bMean;
    out.rStd = std::sqrt(
        std::max(0.0, static_cast<double>(sumR2 / measured.pixelCount) - out.rMean * out.rMean));
    out.gStd = std::sqrt(
        std::max(0.0, static_cast<double>(sumG2 / measured.pixelCount) - out.gMean * out.gMean));
    out.bStd = std::sqrt(
        std::max(0.0, static_cast<double>(sumB2 / measured.pixelCount) - out.bMean * out.bMean));
    out.rOverG = measured.rOverG;
    out.bOverG = measured.bOverG;
    out.pixelCount = measured.pixelCount;
    out.ratiosValid = measured.ratiosValid;
    out.ok = true;
    return true;
}
} // namespace

bool RGBMeanAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageData &pixels = frame.pixels();
    return computeRGB(pixels, mviewer::domain::Selection{0, 0, pixels.width, pixels.height},
                      m_result);
}

bool RGBMeanAnalyzer::analyzeRegion(const ImageFrame &frame,
                                    const mviewer::domain::Selection &region)
{
    return computeRGB(frame.pixels(), region, m_result);
}

std::string RGBMeanAnalyzer::resultText() const
{
    if (!m_result.ok)
        return "RGB mean: unavailable";
    char buf[320];
    if (m_result.ratiosValid)
    {
        std::snprintf(buf, sizeof(buf),
                      "RGB mean: (%.2f, %.2f, %.2f)  std: (%.2f, %.2f, %.2f)  "
                      "R/G %.4f  B/G %.4f",
                      m_result.rMean, m_result.gMean, m_result.bMean, m_result.rStd, m_result.gStd,
                      m_result.bStd, m_result.rOverG, m_result.bOverG);
    }
    else
    {
        std::snprintf(buf, sizeof(buf),
                      "RGB mean: (%.2f, %.2f, %.2f)  std: (%.2f, %.2f, %.2f)  "
                      "R/G —  B/G —",
                      m_result.rMean, m_result.gMean, m_result.bMean, m_result.rStd, m_result.gStd,
                      m_result.bStd);
    }
    return buf;
}

std::unordered_map<std::string, double> RGBMeanAnalyzer::resultMetrics() const
{
    return {{"rMean", m_result.rMean},
            {"gMean", m_result.gMean},
            {"bMean", m_result.bMean},
            {"rStd", m_result.rStd},
            {"gStd", m_result.gStd},
            {"bStd", m_result.bStd},
            {"rOverG", m_result.rOverG},
            {"bOverG", m_result.bOverG},
            {"pixelCount", static_cast<double>(m_result.pixelCount)}};
}
