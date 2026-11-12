#include "core/image/rawplain/PlainRawRender.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mviewer::core::rawplain
{
namespace
{

constexpr double kClipFraction = 0.98;
constexpr int kFullHistogramPixels = 4000000;
constexpr int kHistogramQuadBudget = 1000000;

bool inputOk(const Params &p, const std::vector<std::uint16_t> &samples)
{
    if (!isValid(p) || p.bitDepth < 1 || p.bitDepth > 16)
    {
        return false;
    }
    const std::int64_t need =
        static_cast<std::int64_t>(p.width) * static_cast<std::int64_t>(p.height);
    if (need < 0)
    {
        return false;
    }
    return static_cast<std::size_t>(need) == samples.size();
}

int colourIndex(CfaColor colour)
{
    if (colour == CfaColor::Red)
    {
        return 0;
    }
    if (colour == CfaColor::Blue)
    {
        return 2;
    }
    return 1;
}

void addHistogramSample(std::array<std::vector<std::uint32_t>, 3> &hist, int channel,
                        std::uint16_t sample, int black, int levels, double clipAt)
{
    if (static_cast<double>(sample) >= clipAt)
    {
        return;
    }
    int value = static_cast<int>(sample) - black;
    if (value < 0)
    {
        value = 0;
    }
    if (value >= levels)
    {
        value = levels - 1;
    }
    hist[static_cast<std::size_t>(channel)][static_cast<std::size_t>(value)] += 1U;
}

void accumulateEverySample(const Params &p, const std::vector<std::uint16_t> &samples, int levels,
                           double clipAt, std::array<std::vector<std::uint32_t>, 3> &hist)
{
    const int w = p.width;
    const int h = p.height;
    for (int y = 0; y < h; ++y)
    {
        const int c0 = colourIndex(cfaColorAt(p.cfa, 0, y));
        const int c1 = colourIndex(cfaColorAt(p.cfa, 1, y));
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            const int channel = ((x & 1) == 0) ? c0 : c1;
            const std::uint16_t sample = samples[row + static_cast<std::size_t>(x)];
            addHistogramSample(hist, channel, sample, p.blackLevel, levels, clipAt);
        }
    }
}

// Smallest k with (w/2/k)*(h/2/k) <= kHistogramQuadBudget. Step is 2*k samples
// so each visited 2x2 quad keeps the CFA phase of (0, 0).
int histogramQuadSkip(int w, int h)
{
    const int quadsX = w / 2;
    const int quadsY = h / 2;
    int k = 1;
    if (quadsX < 1 || quadsY < 1)
    {
        return 1;
    }
    while (static_cast<std::int64_t>(quadsX / k) * static_cast<std::int64_t>(quadsY / k) >
           kHistogramQuadBudget)
    {
        ++k;
        if (k > quadsX && k > quadsY)
        {
            break;
        }
    }
    return k;
}

void accumulateOneQuad(const Params &p, const std::vector<std::uint16_t> &samples, int x, int y,
                       int levels, double clipAt, std::array<std::vector<std::uint32_t>, 3> &hist)
{
    const int w = p.width;
    for (int dy = 0; dy < 2; ++dy)
    {
        const int sy = y + dy;
        const int c0 = colourIndex(cfaColorAt(p.cfa, 0, sy));
        const int c1 = colourIndex(cfaColorAt(p.cfa, 1, sy));
        const std::size_t row = static_cast<std::size_t>(sy) * static_cast<std::size_t>(w);
        for (int dx = 0; dx < 2; ++dx)
        {
            const int sx = x + dx;
            const int channel = ((sx & 1) == 0) ? c0 : c1;
            const std::uint16_t sample = samples[row + static_cast<std::size_t>(sx)];
            addHistogramSample(hist, channel, sample, p.blackLevel, levels, clipAt);
        }
    }
}

void accumulateStridedQuads(const Params &p, const std::vector<std::uint16_t> &samples, int levels,
                            double clipAt, std::array<std::vector<std::uint32_t>, 3> &hist)
{
    const int step = histogramQuadSkip(p.width, p.height) * 2;
    const int w = p.width;
    const int h = p.height;
    for (int y = 0; y + 1 < h; y += step)
    {
        for (int x = 0; x + 1 < w; x += step)
        {
            accumulateOneQuad(p, samples, x, y, levels, clipAt, hist);
        }
    }
}

void accumulateHistograms(const Params &p, const std::vector<std::uint16_t> &samples, int white,
                          std::array<std::vector<std::uint32_t>, 3> &hist)
{
    const int levels = 1 << p.bitDepth;
    const std::size_t bins = static_cast<std::size_t>(levels);
    for (auto &channel : hist)
    {
        channel.assign(bins, 0U);
    }
    const double clipAt = static_cast<double>(white) * kClipFraction;
    const std::int64_t pixels =
        static_cast<std::int64_t>(p.width) * static_cast<std::int64_t>(p.height);
    if (pixels > kFullHistogramPixels)
    {
        accumulateStridedQuads(p, samples, levels, clipAt, hist);
        return;
    }
    accumulateEverySample(p, samples, levels, clipAt, hist);
}

double histogramMean(const std::vector<std::uint32_t> &hist)
{
    long double sum = 0.0L;
    std::uint64_t count = 0;
    for (std::size_t i = 0; i < hist.size(); ++i)
    {
        const std::uint64_t n = hist[i];
        count += n;
        sum += static_cast<long double>(n) * static_cast<long double>(i);
    }
    if (count == 0)
    {
        return 0.0;
    }
    return static_cast<double>(sum / static_cast<long double>(count));
}

double whitePatchMean(const std::vector<std::uint32_t> &hist)
{
    std::uint64_t count = 0;
    for (std::uint32_t bin : hist)
    {
        count += bin;
    }
    if (count == 0)
    {
        return 0.0;
    }
    const std::uint64_t rank = ((count - 1U) * 99U) / 100U;
    std::uint64_t acc = 0;
    std::size_t pivot = hist.size() - 1U;
    for (std::size_t i = 0; i < hist.size(); ++i)
    {
        acc += hist[i];
        if (acc > rank)
        {
            pivot = i;
            break;
        }
    }
    long double sum = 0.0L;
    std::uint64_t kept = 0;
    for (std::size_t i = pivot; i < hist.size(); ++i)
    {
        const std::uint64_t n = hist[i];
        kept += n;
        sum += static_cast<long double>(n) * static_cast<long double>(i);
    }
    if (kept == 0)
    {
        return 0.0;
    }
    return static_cast<double>(sum / static_cast<long double>(kept));
}

double clampedRatio(double numerator, double denominator)
{
    if (!(numerator > 0.0) || !(denominator > 0.0))
    {
        return 1.0;
    }
    double gain = numerator / denominator;
    if (gain < 0.1)
    {
        gain = 0.1;
    }
    if (gain > 16.0)
    {
        gain = 16.0;
    }
    return gain;
}

WbGains gainsFromMeans(double meanR, double meanG, double meanB)
{
    WbGains gains;
    gains.r = clampedRatio(meanG, meanR);
    gains.g = 1.0;
    gains.b = clampedRatio(meanG, meanB);
    return gains;
}

} // namespace

WbGains computeWhiteBalance(const Params &p, const std::vector<std::uint16_t> &samples)
{
    WbGains unit;
    if (!inputOk(p, samples) || p.cfa == Cfa::Mono || p.wb == WhiteBalance::None)
    {
        return unit;
    }
    if (p.wb == WhiteBalance::Manual)
    {
        unit.r = p.wbGainR;
        unit.b = p.wbGainB;
        return unit;
    }
    const int white = effectiveWhiteLevel(p);
    std::array<std::vector<std::uint32_t>, 3> hist;
    accumulateHistograms(p, samples, white, hist);
    const bool patch = p.wb == WhiteBalance::WhitePatch;
    const double meanR = patch ? whitePatchMean(hist[0]) : histogramMean(hist[0]);
    const double meanG = patch ? whitePatchMean(hist[1]) : histogramMean(hist[1]);
    const double meanB = patch ? whitePatchMean(hist[2]) : histogramMean(hist[2]);
    return gainsFromMeans(meanR, meanG, meanB);
}

} // namespace mviewer::core::rawplain
