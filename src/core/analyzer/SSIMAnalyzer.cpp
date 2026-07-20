#include "core/analyzer/SSIMAnalyzer.h"

#include "core/analysis/AnalysisEngine.h"

bool SSIMAnalyzer::analyze(const ImageFrame &frame)
{
    if (!m_ref || m_ref->pixels().isNull() || frame.pixels().isNull())
        return false;
    m_ssim = AnalysisEngine::ssim(m_ref->pixels(), frame.pixels());
    return true;
}

bool SSIMAnalyzer::analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region)
{
    if (!m_ref || m_ref->pixels().isNull() || frame.pixels().isNull() || region.isEmpty())
        return false;
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
    const int rh = y1 - y0;
    ImageData subR = makeImageData(rw, rh, m_ref->pixels().format);
    ImageData subT = makeImageData(rw, rh, frame.pixels().format);
    const int cppR = vR.channelsPerPixel();
    const int cppT = vT.channelsPerPixel();
    for (int y = 0; y < rh; ++y)
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
    m_ssim = AnalysisEngine::ssim(subR, subT);
    return true;
}

std::string SSIMAnalyzer::resultText() const
{
    return "ssim: " + std::to_string(m_ssim);
}
