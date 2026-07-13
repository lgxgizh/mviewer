#include "core/analyzer/SharpnessAnalyzer.h"
#include <cmath>
#include <cstring>

double SharpnessAnalyzer::computeSharpness(const ImageBuffer& v, int x0, int y0, int x1, int y1) const {
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    if (x0 < 1) x0 = 1;
    if (y0 < 1) y0 = 1;
    if (x1 >= w) x1 = w-1;
    if (y1 >= h) y1 = h-1;
    if (x1 <= x0 || y1 <= y0) return 0.0;
    const int64_t n = static_cast<int64_t>(x1-x0)*(y1-y0);
    double sumG = 0.0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto lum = [&](int xx, int yy) {
                const uint8_t* p = v.data + static_cast<size_t>(yy)*v.stride() + static_cast<size_t>(xx)*cpp;
                return (p[0]+p[1]+p[2]) / 3.0;
            };
            // Sobel: Gx = (r+2l+r) - (r+2l+r) shifted cols
            const double gx = (lum(x+1,y-1) + 2*lum(x+1,y) + lum(x+1,y+1)) -
                              (lum(x-1,y-1) + 2*lum(x-1,y) + lum(x-1,y+1));
            const double gy = (lum(x-1,y+1) + 2*lum(x,y+1) + lum(x+1,y+1)) -
                              (lum(x-1,y-1) + 2*lum(x,y-1) + lum(x+1,y-1));
            sumG += std::sqrt(gx*gx + gy*gy);
        }
    }
    return sumG / n;
}

bool SharpnessAnalyzer::analyze(const ImageFrame& frame) {
    if (frame.pixels().isNull()) return false;
    const ImageBuffer v = frame.pixels().view();
    m_sharp = computeSharpness(v, 0, 0, v.width, v.height);
    return true;
}

bool SharpnessAnalyzer::analyzeRegion(const ImageFrame& frame, const mviewer::domain::Selection& region) {
    if (frame.pixels().isNull() || region.isEmpty()) return false;
    const ImageBuffer v = frame.pixels().view();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width,  region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
    if (x1 <= x0 || y1 <= y0) return false;
    m_sharp = computeSharpness(v, x0, y0, x1, y1);
    return true;
}
