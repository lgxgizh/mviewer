#include "core/analyzer/ColorCheckerAnalyzer.h"

#include <cmath>
#include <cstring>

namespace
{

// Conversion from sRGB (0..255) to CIE L*a*b* under D65 standard illuminant.
inline double sRgbToLinear(double c)
{
    c /= 255.0;
    return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

inline void rgbToXyz(double r, double g, double b, double &X, double &Y, double &Z)
{
    const double lr = sRgbToLinear(r);
    const double lg = sRgbToLinear(g);
    const double lb = sRgbToLinear(b);
    X = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) * 100.0;
    Y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) * 100.0;
    Z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) * 100.0;
}

inline double fLab(double t)
{
    constexpr double delta = 6.0 / 29.0;
    return (t > delta * delta * delta) ? std::cbrt(t) : (t / (3.0 * delta * delta) + 4.0 / 29.0);
}

inline void xyzToLab(double X, double Y, double Z, double &outL, double &outA, double &outB)
{
    constexpr double Xn = 95.047;
    constexpr double Yn = 100.000;
    constexpr double Zn = 108.883;

    const double fx = fLab(X / Xn);
    const double fy = fLab(Y / Yn);
    const double fz = fLab(Z / Zn);

    outL = 116.0 * fy - 16.0;
    outA = 500.0 * (fx - fy);
    outB = 200.0 * (fy - fz);
}

inline void rgbToLab(double r, double g, double b, double &outL, double &outA, double &outB)
{
    double X = 0.0, Y = 0.0, Z = 0.0;
    rgbToXyz(r, g, b, X, Y, Z);
    xyzToLab(X, Y, Z, outL, outA, outB);
}

// Standard CIE76 Delta-E in L*a*b* space.
double deltaE(double r1, double g1, double b1, double r2, double g2, double b2)
{
    double L1 = 0.0, a1 = 0.0, labB1 = 0.0;
    double L2 = 0.0, a2 = 0.0, labB2 = 0.0;
    rgbToLab(r1, g1, b1, L1, a1, labB1);
    rgbToLab(r2, g2, b2, L2, a2, labB2);
    const double dL = L1 - L2;
    const double da = a1 - a2;
    const double db = labB1 - labB2;
    return std::sqrt(dL * dL + da * da + db * db);
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
        m_maxDE = 0.0;
        m_maxPatch = -1;
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
        m_maxDE = 0.0;
        m_maxPatch = -1;
        m_patches = 0;
        return false;
    }
    const int cpp = v.channelsPerPixel();
    double sumDE = 0.0;
    double maxDE = 0.0;
    int maxPatch = -1;
    int n = 0;
    const int radX = std::max(1, cw / 4);
    const int radY = std::max(1, ch / 4);

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const int cx = x0 + c * cw + cw / 2;
            const int cy = y0 + r * ch + ch / 2;
            // Average center 25% area of patch for noise immunity.
            double sr = 0, sg = 0, sb = 0;
            int cnt = 0;
            for (int dy = -radY; dy <= radY; ++dy)
            {
                for (int dx = -radX; dx <= radX; ++dx)
                {
                    const int px = cx + dx, py = cy + dy;
                    if (px < 0 || py < 0 || px >= v.width || py >= v.height)
                        continue;
                    const uint8_t *p = v.data + static_cast<size_t>(py) * v.stride() +
                                       static_cast<size_t>(px) * cpp;
                    uint8_t pr = 0, pg = 0, pb = 0;
                    getPixelRGB(p, v.format, pr, pg, pb);
                    sr += pr;
                    sg += pg;
                    sb += pb;
                    ++cnt;
                }
            }
            if (cnt == 0)
                continue;
            const double ar = sr / cnt, ag = sg / cnt, ab = sb / cnt;
            const int patchIdx = r * cols + c;
            const double *ref = kRef[patchIdx];
            const double de = deltaE(ar, ag, ab, ref[0], ref[1], ref[2]);
            sumDE += de;
            if (de > maxDE)
            {
                maxDE = de;
                maxPatch = patchIdx + 1;
            }
            ++n;
        }
    }
    m_patches = n;
    m_meanDE = n > 0 ? sumDE / n : 0.0;
    m_maxDE = maxDE;
    m_maxPatch = maxPatch;
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
    const long long x0ll = std::clamp<long long>(region.x, 0, v.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, v.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, v.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, v.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return false;
    return compute(v, x0, y0, x1, y1);
}

std::string ColorCheckerAnalyzer::resultText() const
{
    char buf[160];
    if (m_maxPatch > 0)
    {
        std::snprintf(buf, sizeof(buf),
                      "colorchecker: %d patches, mean Delta-E %.2f, max Delta-E %.2f (patch #%d)",
                      m_patches, m_meanDE, m_maxDE, m_maxPatch);
    }
    else
    {
        std::snprintf(buf, sizeof(buf),
                      "colorchecker: %d patches, mean Delta-E %.2f, max Delta-E %.2f", m_patches,
                      m_meanDE, m_maxDE);
    }
    return std::string(buf);
}

std::unordered_map<std::string, double> ColorCheckerAnalyzer::resultMetrics() const
{
    return {{"meanDeltaE", m_meanDE},
            {"maxDeltaE", m_maxDE},
            {"maxDeltaEPatch", static_cast<double>(m_maxPatch)},
            {"patchCount", static_cast<double>(m_patches)}};
}
