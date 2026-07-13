#include "core/analyzer/EntropyAnalyzer.h"
#include <cmath>
#include <cstring>

double EntropyAnalyzer::computeEntropy(const ImageBuffer& v, int x0, int y0, int x1, int y1) const {
    if (x1 <= x0 || y1 <= y0) return 0.0;
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    int hist[256] = {0};
    const int64_t n = static_cast<int64_t>(x1-x0)*(y1-y0);
    if (n == 0) return 0.0;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* line = v.data + static_cast<size_t>(y)*v.stride();
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = line + static_cast<size_t>(x)*cpp;
            ++hist[(p[0]+p[1]+p[2]) / 3];
        }
    }
    double H = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (hist[i] == 0) continue;
        const double p = static_cast<double>(hist[i]) / n;
        H -= p * std::log2(p);
    }
    return H;
}

bool EntropyAnalyzer::analyze(const ImageFrame& frame) {
    if (frame.pixels().isNull()) return false;
    const ImageBuffer v = frame.pixels().view();
    m_entropy = computeEntropy(v, 0, 0, v.width, v.height);
    return true;
}

bool EntropyAnalyzer::analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) {
    if (frame.pixels().isNull() || region.isEmpty()) return false;
    const ImageBuffer v = frame.pixels().view();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width,  region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
    if (x1 <= x0 || y1 <= y0) return false;
    m_entropy = computeEntropy(v, x0, y0, x1, y1);
    return true;
}
