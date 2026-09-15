#include "core/analyzer/HistogramAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{

mviewer::domain::Histogram toHistogram(const ImageStats &s)
{
    mviewer::domain::Histogram h;
    for (int i = 0; i < 256; ++i)
    {
        h.luminance[i] = s.histLum[i];
        h.red[i] = s.histR[i];
        h.green[i] = s.histG[i];
        h.blue[i] = s.histB[i];
        h.v[i] = s.histV[i];
    }
    h.lumMean = s.lumMean;
    h.vMean = s.vMean;
    h.rMean = s.rMean;
    h.gMean = s.gMean;
    h.bMean = s.bMean;
    return h;
}

} // namespace

bool HistogramAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    m_result = toHistogram(AnalysisEngine::computeStats(frame.pixels()));
    return true;
}

bool HistogramAnalyzer::analyzeRegion(const ImageFrame &frame,
                                      const mviewer::domain::Selection &region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;

    const ImageStats stats = AnalysisEngine::computeStatsROI(frame.pixels(), region);
    if (stats.pixelCount <= 0)
        return false;

    m_result = toHistogram(stats);
    return true;
}

std::string HistogramAnalyzer::resultText() const
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "lumMean: %.1f  V: %.1f  R: %.1f  G: %.1f  B: %.1f",
                  m_result.lumMean, m_result.vMean, m_result.rMean, m_result.gMean,
                  m_result.bMean);
    return buf;
}

std::unordered_map<std::string, double> HistogramAnalyzer::resultMetrics() const
{
    return {{"lumMean", m_result.lumMean},
            {"vMean", m_result.vMean},
            {"rMean", m_result.rMean},
            {"gMean", m_result.gMean},
            {"bMean", m_result.bMean}};
}
