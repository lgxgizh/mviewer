#include "core/analyzer/PSNRAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

bool PSNRAnalyzer::analyze(const ImageFrame &frame)
{
    if (!m_ref || m_ref->pixels().isNull() || frame.pixels().isNull())
        return false;
    m_psnr = AnalysisEngine::psnr(m_ref->pixels(), frame.pixels());
    return true;
}

bool PSNRAnalyzer::analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region)
{
    if (!m_ref || m_ref->pixels().isNull() || frame.pixels().isNull() || region.isEmpty())
        return false;
    // Extract ROI from both, then compute PSNR.
    const ImageBuffer vR = m_ref->pixels().view();
    const ImageBuffer vT = frame.pixels().view();
    const int w = std::min(vR.width, vT.width);
    const int h = std::min(vR.height, vT.height);
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(w, region.x + region.width);
    const int y1 = std::min(h, region.y + region.height);
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int rw = x1 - x0;
    const int rw2 = y1 - y0;
    ImageData subR = makeImageData(rw, rw2, m_ref->pixels().format);
    ImageData subT = makeImageData(rw, rw2, frame.pixels().format);
    const int cppR = vR.channelsPerPixel();
    const int cppT = vT.channelsPerPixel();
    for (int y = 0; y < rw2; ++y)
    {
        std::memcpy(subR.buffer->data() + static_cast<size_t>(y) * rw * cppR,
                    vR.data + static_cast<size_t>(y0 + y) * vR.stride() +
                        static_cast<size_t>(x0) * cppR,
                    static_cast<size_t>(rw) * cppR);
        std::memcpy(subT.buffer->data() + static_cast<size_t>(y) * rw * cppT,
                    vT.data + static_cast<size_t>(y0 + y) * vT.stride() +
                        static_cast<size_t>(x0) * cppT,
                    static_cast<size_t>(rw) * cppT);
    }
    m_psnr = AnalysisEngine::psnr(subR, subT);
    return true;
}

std::string PSNRAnalyzer::resultText() const
{
    return "psnr (dB): " + std::to_string(m_psnr);
}

std::unordered_map<std::string, double> PSNRAnalyzer::resultMetrics() const
{
    // PSNR is a dual-image metric: only meaningful once a reference has been
    // supplied via setReference() before analyze(). Batch runBatch() supplies
    // no reference, so report an empty metric map rather than a misleading 0.
    if (m_ref == nullptr)
        return {};
    return {{"psnr", m_psnr}};
}
