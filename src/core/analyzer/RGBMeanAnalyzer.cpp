#include "core/analyzer/RGBMeanAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

#include <cmath>

// NOTE: RGBMeanAnalyzer uses AnalysisEngine::computeStats (ROI) for both
// full-frame and region analysis; mean/std are derived from the histogram.
// AnalysisEngine::computeStats already computes histo + lumMean, so we extend
// to compute per-channel stats directly for precision.
bool RGBMeanAnalyzer::analyze(const ImageFrame& frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(w) * h;
    if (n == 0)
        return false;
    double sumR = 0, sumG = 0, sumB = 0;
    double sumR2 = 0, sumG2 = 0, sumB2 = 0;
    for (int y = 0; y < h; ++y)
    {
        const uint8_t* line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < w; ++x)
        {
            const uint8_t* p = line + static_cast<size_t>(x) * cpp;
            const double r = p[0], g = p[1], b = p[2];
            sumR += r;
            sumG += g;
            sumB += b;
            sumR2 += r * r;
            sumG2 += g * g;
            sumB2 += b * b;
        }
    }
    m_result.rMean = sumR / n;
    m_result.gMean = sumG / n;
    m_result.bMean = sumB / n;
    m_result.rStd = std::sqrt(sumR2 / n - m_result.rMean * m_result.rMean);
    m_result.gStd = std::sqrt(sumG2 / n - m_result.gMean * m_result.gMean);
    m_result.bStd = std::sqrt(sumB2 / n - m_result.bMean * m_result.bMean);
    m_result.ok = true;
    return true;
}

bool RGBMeanAnalyzer::analyzeRegion(const ImageFrame& frame,
    const mviewer::domain::Selection& region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;
    const ImageBuffer v = frame.pixels().view();
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(w, region.x + region.width);
    const int y1 = std::min(h, region.y + region.height);
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);
    if (n == 0)
        return false;
    double sumR = 0, sumG = 0, sumB = 0;
    double sumR2 = 0, sumG2 = 0, sumB2 = 0;
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t* line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t* p = line + static_cast<size_t>(x) * cpp;
            const double r = p[0], g = p[1], b = p[2];
            sumR += r;
            sumG += g;
            sumB += b;
            sumR2 += r * r;
            sumG2 += g * g;
            sumB2 += b * b;
        }
    }
    m_result.rMean = sumR / n;
    m_result.gMean = sumG / n;
    m_result.bMean = sumB / n;
    m_result.rStd = std::sqrt(sumR2 / n - m_result.rMean * m_result.rMean);
    m_result.gStd = std::sqrt(sumG2 / n - m_result.gMean * m_result.gMean);
    m_result.bStd = std::sqrt(sumB2 / n - m_result.bMean * m_result.bMean);
    m_result.ok = true;
    return true;
}
