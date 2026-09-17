#include "core/analyzer/MTFAnalyzer.h"

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
    if (w < 8 || h < 4)
    {
        m_mtf50 = 0.0;
        m_mtf50Cps = 0.0;
        return false;
    }

    // Build an Edge Spread Function (ESF) from a vertical edge: for each row,
    // average the luminance across the width to get a 1-D profile that
    // transitions dark->bright. Then differentiate to the Line Spread (LSF)
    // and FFT to the MTF.
    std::vector<double> profile(static_cast<size_t>(h), 0.0);
    for (int y = y0; y < y1; ++y)
    {
        double row = 0.0;
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p =
                v.data + static_cast<size_t>(y) * v.stride() + static_cast<size_t>(x) * cpp;
            row += pixelLuminanceAvg(p, v.format);
        }
        profile[y - y0] = row / w;
    }
    // Normalize the profile to [0,1].
    double lo = profile.front(), hi = profile.front();
    for (double d : profile)
    {
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    if (hi - lo < 1e-3)
    {
        m_mtf50 = 0.0;
        m_mtf50Cps = 0.0;
        return false;
    }
    for (double &d : profile)
        d = (d - lo) / (hi - lo);

    // Differentiate ESF to obtain the Line Spread Function (LSF).
    std::vector<double> lsf(static_cast<size_t>(h), 0.0);
    for (size_t y = 0; y + 1 < static_cast<size_t>(h); ++y)
    {
        lsf[y] = std::abs(profile[y + 1] - profile[y]);
    }
    lsf[static_cast<size_t>(h) - 1] = 0.0;

    // Pad LSF to the next power of two for the FFT.
    size_t n = 1;
    while (n < static_cast<size_t>(h))
        n <<= 1;
    std::vector<double> re(n, 0.0), im(n, 0.0);
    for (size_t i = 0; i < static_cast<size_t>(h); ++i)
        re[i] = lsf[i];
    fft(re, im);

    // MTF = magnitude spectrum (single-sided up to Nyquist = n/2 bins).
    const size_t half = n / 2;
    std::vector<double> mag(half, 0.0);
    for (size_t i = 0; i < half; ++i)
        mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);
    // Low-frequency reference (average of the first few bins starting at DC).
    const size_t lowN = std::max<size_t>(1, half / 20);
    double low = 0.0;
    for (size_t i = 0; i < lowN; ++i)
        low += mag[i];
    low /= static_cast<double>(lowN);
    if (low < 1e-6)
    {
        m_mtf50 = 0.0;
        m_mtf50Cps = 0.0;
        return false;
    }
    // Find the first spatial frequency where MTF drops to 50% of low.
    // bin i corresponds to i/N cycles-per-pixel-unit; Nyquist = half -> 0.5.
    // If the edge transition is sharp and never drops below 50% before Nyquist,
    // default to Nyquist (0.5 c/px -> MTF50 = 1.0).
    double mtf50Cps = 0.5;
    for (size_t i = 1; i < half; ++i)
    {
        if (mag[i] <= 0.5 * low)
        {
            // Linear interpolate the crossing between bin i-1 and i.
            const double m0 = mag[i - 1];
            const double m1 = mag[i];
            const double frac = (m0 - 0.5 * low) / (m0 - m1 + 1e-9);
            const double bin = static_cast<double>(i - 1) + frac;
            mtf50Cps = bin / static_cast<double>(n); // cycles per pixel
            break;
        }
    }
    m_mtf50Cps = mtf50Cps;
    // Normalize to Nyquist (0.5 c/p) -> [0,1].
    m_mtf50 = std::min(1.0, (mtf50Cps / 0.5));
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
