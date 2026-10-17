#include "core/analyzer/MTFAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{

// In-place iterative radix-2 FFT (Cooley-Tukey) on complex interleaved
// [re,im,...] of length n (power of two). Standard textbook impl.
void fft(std::vector<double> &re, std::vector<double> &im)
{
    const size_t n = re.size();
    if (n <= 1)
        return;
    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const double wRe = std::cos(ang);
        const double wIm = std::sin(ang);
        for (size_t i = 0; i < n; i += len)
        {
            double curRe = 1.0, curIm = 0.0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const size_t a = i + k;
                const size_t b = i + k + len / 2;
                const double tRe = re[b] * curRe - im[b] * curIm;
                const double tIm = re[b] * curIm + im[b] * curRe;
                re[b] = re[a] - tRe;
                im[b] = im[a] - tIm;
                re[a] += tRe;
                im[a] += tIm;
                const double nxtRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nxtRe;
            }
        }
    }
}

std::vector<double> extractEdgeProfile(const ImageBuffer &v, int x0, int y0, int x1, int y1,
                                       int cpp)
{
    const int w = x1 - x0;
    const int h = y1 - y0;
    const int qW = std::max(1, w / 4);
    const int qH = std::max(1, h / 4);
    double leftSum = 0.0, rightSum = 0.0;
    double topSum = 0.0, bottomSum = 0.0;

    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x0 + qW; ++x)
        {
            const uint8_t *p =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            leftSum += pixelLuminanceAvg(p, v.format);
        }
        for (int x = x1 - qW; x < x1; ++x)
        {
            const uint8_t *p =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            rightSum += pixelLuminanceAvg(p, v.format);
        }
    }
    const double diffX = std::abs((rightSum - leftSum) / (static_cast<double>(qW * h)));

    for (int x = x0; x < x1; ++x)
    {
        for (int y = y0; y < y0 + qH; ++y)
        {
            const uint8_t *p =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            topSum += pixelLuminanceAvg(p, v.format);
        }
        for (int y = y1 - qH; y < y1; ++y)
        {
            const uint8_t *p =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            bottomSum += pixelLuminanceAvg(p, v.format);
        }
    }
    const double diffY = std::abs((bottomSum - topSum) / (static_cast<double>(w * qH)));
    const bool isVerticalEdge = (diffX > diffY);

    const size_t profLen = isVerticalEdge ? static_cast<size_t>(w) : static_cast<size_t>(h);
    if (profLen < 8)
        return {};

    std::vector<double> profile(profLen, 0.0);
    if (isVerticalEdge)
    {
        for (int x = x0; x < x1; ++x)
        {
            double col = 0.0;
            for (int y = y0; y < y1; ++y)
            {
                const uint8_t *p =
                    v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
                col += pixelLuminanceAvg(p, v.format);
            }
            profile[static_cast<size_t>(x - x0)] = col / h;
        }
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            double row = 0.0;
            for (int x = x0; x < x1; ++x)
            {
                const uint8_t *p =
                    v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
                row += pixelLuminanceAvg(p, v.format);
            }
            profile[static_cast<size_t>(y - y0)] = row / w;
        }
    }
    return profile;
}

bool calculateMtf50(std::vector<double> &profile, double &outMtf50, double &outMtf50Cps)
{
    double lo = profile.front(), hi = profile.front();
    for (double d : profile)
    {
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    if (hi - lo < 1e-3)
        return false;

    for (double &d : profile)
        d = (d - lo) / (hi - lo);

    const size_t profLen = profile.size();
    std::vector<double> lsf(profLen, 0.0);
    for (size_t i = 0; i + 1 < profLen; ++i)
        lsf[i] = std::abs(profile[i + 1] - profile[i]);
    lsf[profLen - 1] = 0.0;

    if (profLen > 1)
    {
        const auto maxIt = std::max_element(lsf.begin(), lsf.end());
        const size_t peakIdx = static_cast<size_t>(std::distance(lsf.begin(), maxIt));
        const double radius = std::max(4.0, static_cast<double>(profLen) * 0.5);
        for (size_t i = 0; i < profLen; ++i)
        {
            const double dist = std::abs(static_cast<double>(i) - static_cast<double>(peakIdx));
            if (dist >= radius)
            {
                lsf[i] = 0.0;
            }
            else
            {
                const double wnd = 0.54 + 0.46 * std::cos(M_PI * dist / radius);
                lsf[i] *= wnd;
            }
        }
    }

    size_t n = 1;
    while (n < profLen)
        n <<= 1;
    std::vector<double> re(n, 0.0), im(n, 0.0);
    for (size_t i = 0; i < profLen; ++i)
        re[i] = lsf[i];
    fft(re, im);

    const size_t half = n / 2;
    std::vector<double> mag(half, 0.0);
    for (size_t i = 0; i < half; ++i)
        mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);

    const size_t lowN = std::max<size_t>(1, half / 20);
    double low = 0.0;
    for (size_t i = 0; i < lowN; ++i)
        low += mag[i];
    low /= static_cast<double>(lowN);
    if (low < 1e-6)
        return false;

    double mtf50Cps = 0.5;
    for (size_t i = 1; i < half; ++i)
    {
        if (mag[i] <= 0.5 * low)
        {
            const double m0 = mag[i - 1];
            const double m1 = mag[i];
            const double frac = (m0 - 0.5 * low) / (m0 - m1 + 1e-9);
            const double bin = static_cast<double>(i - 1) + frac;
            mtf50Cps = bin / static_cast<double>(n);
            break;
        }
    }
    outMtf50Cps = mtf50Cps;
    outMtf50 = std::min(1.0, (mtf50Cps / 0.5));
    return true;
}

} // namespace

bool MTFAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > v.width)
        x1 = v.width;
    if (y1 > v.height)
        y1 = v.height;
    const int cpp = v.channelsPerPixel();
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 4 || h < 4 || (w < 8 && h < 8))
    {
        m_mtf50 = 0.0;
        m_mtf50Cps = 0.0;
        return false;
    }

    std::vector<double> profile = extractEdgeProfile(v, x0, y0, x1, y1, cpp);
    if (profile.empty() || !calculateMtf50(profile, m_mtf50, m_mtf50Cps))
    {
        m_mtf50 = 0.0;
        m_mtf50Cps = 0.0;
        return false;
    }
    return true;
}

bool MTFAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    compute(v, 0, 0, v.width, v.height);
    return true;
}

bool MTFAnalyzer::analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &region)
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
    compute(v, x0, y0, x1, y1);
    return true;
}

std::string MTFAnalyzer::resultText() const
{
    if (m_mtf50 <= 0.0 && m_mtf50Cps <= 0.0)
        return "MTF50: no edge detected (uniform image)";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "MTF50=%.3f (Nyquist)  %.4f c/px", m_mtf50, m_mtf50Cps);
    return std::string(buf);
}

std::unordered_map<std::string, double> MTFAnalyzer::resultMetrics() const
{
    return {{"mtf50", m_mtf50}, {"mtf50CyclesPerPixel", m_mtf50Cps}};
}
