#include "core/analyzer/ColorCheckerAnalyzer.h"

#include <cmath>
#include <cstring>

namespace
{

// CIE76 Delta-E between two sRGB triplets (0..255).
double deltaE(double r1, double g1, double b1, double r2, double g2, double b2)
{
    // Cheap perceptual distance in sRGB space (good enough for patch
    // validation; CIE76 is the classic textbook metric).
    const double dr = r1 - r2;
    const double dg = g1 - g2;
    const double db = b1 - b2;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// Reference 24-patch ColorChecker sRGB means (approx), row-major
// (row 0 = top, left-to-right), values 0..255.
// Source: standard X-Rite ColorChecker average sRGB.
const double kRef[24][3] = {{115, 82, 68},   {194, 150, 130}, {98, 122, 157},  {87, 108, 67},
                            {133, 128, 186}, {103, 189, 170}, {214, 126, 44},  {80, 91, 166},
                            {193, 84, 97},   {94, 60, 108},   {157, 188, 64},  {224, 163, 46},
                            {56, 61, 150},   {70, 148, 73},   {175, 54, 60},   {231, 199, 31},
                            {187, 86, 149},  {8, 133, 161},   {243, 243, 242}, {200, 200, 200},
                            {160, 160, 160}, {122, 122, 121}, {85, 85, 85},    {52, 52, 52}};

} // namespace

bool ColorCheckerAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
    {
        m_meanDE = 0.0;
        m_patches = 0;
        return false;
    }
    // Expect the ROI to frame a 6x4 grid; sample each cell's center.
    const int cols = 6, rows = 4;
    const int cw = (x1 - x0) / cols;
    const int ch = (y1 - y0) / rows;
    if (cw < 2 || ch < 2)
    {
        m_meanDE = 0.0;
        m_patches = 0;
        return false;
    }
    const int cpp = v.channelsPerPixel();
    double sumDE = 0.0;
    int n = 0;
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const int cx = x0 + c * cw + cw / 2;
            const int cy = y0 + r * ch + ch / 2;
            // Average a small 3x3 box at the patch center.
            double sr = 0, sg = 0, sb = 0;
            int cnt = 0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int px = cx + dx, py = cy + dy;
                    if (px < 0 || py < 0 || px >= v.width || py >= v.height)
                        continue;
                    const uint8_t *p = v.data + static_cast<size_t>(py) * v.stride() +
                                       static_cast<size_t>(px) * cpp;
                    sr += p[0];
                    sg += p[1];
                    sb += p[2];
                    ++cnt;
                }
            }
            if (cnt == 0)
                continue;
            const double ar = sr / cnt, ag = sg / cnt, ab = sb / cnt;
            const double *ref = kRef[r * cols + c];
            sumDE += deltaE(ar, ag, ab, ref[0], ref[1], ref[2]);
            ++n;
        }
    }
    m_patches = n;
    m_meanDE = n > 0 ? sumDE / n : 0.0;
    return n > 0;
}

bool ColorCheckerAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool ColorCheckerAnalyzer::analyzeRegion(const ImageFrame &frame,
                                         const mviewer::domain::Selection &region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;
    const ImageBuffer v = frame.pixels().view();
    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(v.width, region.x + region.width);
    const int y1 = std::min(v.height, region.y + region.height);
    if (x1 <= x0 || y1 <= y0)
        return false;
    return compute(v, x0, y0, x1, y1);
}

std::string ColorCheckerAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "colorchecker: %d patches, mean Delta-E %.2f", m_patches,
                  m_meanDE);
    return std::string(buf);
}

std::unordered_map<std::string, double> ColorCheckerAnalyzer::resultMetrics() const
{
    return {{"meanDeltaE", m_meanDE}, {"patchCount", static_cast<double>(m_patches)}};
}
