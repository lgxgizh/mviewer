#include "core/analyzer/HistogramAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

#include <algorithm>
#include <cstring>

namespace
{

mviewer::domain::Histogram toHistogram(const ImageStats& s)
{
    mviewer::domain::Histogram h;
    for (int i = 0; i < 256; ++i)
    {
        h.luminance[i] = s.histLum[i];
        h.red[i] = s.histR[i];
        h.green[i] = s.histG[i];
        h.blue[i] = s.histB[i];
    }
    h.lumMean = s.lumMean;
    h.rMean = s.rMean;
    h.gMean = s.gMean;
    h.bMean = s.bMean;
    return h;
}

} // namespace

bool HistogramAnalyzer::analyze(const ImageFrame& frame)
{
    if (frame.pixels().isNull())
        return false;
    m_result = toHistogram(AnalysisEngine::computeStats(frame.pixels()));
    return true;
}

bool HistogramAnalyzer::analyzeRegion(const ImageFrame& frame,
    const mviewer::domain::Selection& region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;

    const ImageBuffer v = frame.pixels().view();
    const int cpp = v.channelsPerPixel();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
    if (x1 <= x0 || y1 <= y0)
        return false;

    mviewer::domain::Histogram h;
    std::memset(&h, 0, sizeof(h));
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);

    for (int y = y0; y < y1; ++y)
    {
        const uint8_t* line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t* p = line + static_cast<size_t>(x) * cpp;
            const int r = p[0], g = p[1], b = p[2];
            ++h.luminance[std::clamp(luminance(r, g, b), 0, 255)];
            ++h.red[std::clamp(r, 0, 255)];
            ++h.green[std::clamp(g, 0, 255)];
            ++h.blue[std::clamp(b, 0, 255)];
            sumR += r;
            sumG += g;
            sumB += b;
            sumL += luminance(r, g, b);
        }
    }
    h.lumMean = static_cast<double>(sumL) / n;
    h.rMean = static_cast<double>(sumR) / n;
    h.gMean = static_cast<double>(sumG) / n;
    h.bMean = static_cast<double>(sumB) / n;
    m_result = h;
    return true;
}

namespace
{
// Auto-register with the analyzer registry on load.
struct HistogramAnalyzerRegistrar
{
    HistogramAnalyzerRegistrar()
    {
        AnalyzerRegistry::instance().registerAnalyzer("histogram",
            []() -> std::unique_ptr<Analyzer> { return std::make_unique<HistogramAnalyzer>(); });
    }
} g_histogramAnalyzerRegistrar;
} // namespace
