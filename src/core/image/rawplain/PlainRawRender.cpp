#include "core/image/rawplain/PlainRawRender.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace mviewer::core::rawplain
{
namespace
{

enum class SiteKind : std::uint8_t
{
    Red,
    Blue,
    GreenH,
    GreenV
};

constexpr double kClipFraction = 0.98;
constexpr int kLinearMax = 65535;

int mirrorIndex(int i, int n)
{
    if (n <= 1)
    {
        return 0;
    }
    if (i < 0)
    {
        return 1;
    }
    if (i >= n)
    {
        return n - 2;
    }
    return i;
}

double channelGain(const WbGains &gains, CfaColor colour)
{
    if (colour == CfaColor::Red)
    {
        return gains.r;
    }
    if (colour == CfaColor::Blue)
    {
        return gains.b;
    }
    return gains.g;
}

std::uint16_t linearCode(int sample, int black, int white, double colourGain, double gain)
{
    const double range = static_cast<double>(white - black);
    double t = 0.0;
    if (range > 0.0)
    {
        t = (static_cast<double>(sample) - static_cast<double>(black)) / range;
    }
    if (t < 0.0)
    {
        t = 0.0;
    }
    if (t > 1.0)
    {
        t = 1.0;
    }
    t *= colourGain * gain;
    if (!std::isfinite(t) || t < 0.0)
    {
        t = 0.0;
    }
    if (t > 1.0)
    {
        t = 1.0;
    }
    long rounded = std::lround(t * static_cast<double>(kLinearMax));
    if (rounded < 0)
    {
        rounded = 0;
    }
    if (rounded > kLinearMax)
    {
        rounded = kLinearMax;
    }
    return static_cast<std::uint16_t>(rounded);
}

void buildPositionLuts(const Params &p, const WbGains &gains, int white,
                       std::array<std::vector<std::uint16_t>, 4> &luts)
{
    const int levels = 1 << p.bitDepth;
    for (int py = 0; py < 2; ++py)
    {
        for (int px = 0; px < 2; ++px)
        {
            const int slot = py * 2 + px;
            const double cg = channelGain(gains, cfaColorAt(p.cfa, px, py));
            luts[static_cast<std::size_t>(slot)].resize(static_cast<std::size_t>(levels));
            auto &table = luts[static_cast<std::size_t>(slot)];
            for (int s = 0; s < levels; ++s)
            {
                table[static_cast<std::size_t>(s)] = linearCode(s, p.blackLevel, white, cg, p.gain);
            }
        }
    }
}

std::uint16_t lookupSample(const std::vector<std::uint16_t> &table, std::uint16_t sample)
{
    std::size_t index = sample;
    if (table.empty())
    {
        return 0;
    }
    if (index >= table.size())
    {
        index = table.size() - 1;
    }
    return table[index];
}

void buildEncodeLut(bool srgbGamma, std::vector<std::uint8_t> &lut)
{
    lut.resize(static_cast<std::size_t>(kLinearMax) + 1U);
    for (int i = 0; i <= kLinearMax; ++i)
    {
        int code = 0;
        if (srgbGamma)
        {
            const double encoded =
                srgbEncode(static_cast<double>(i) / static_cast<double>(kLinearMax));
            long rounded = std::lround(encoded * 255.0);
            if (rounded < 0)
            {
                rounded = 0;
            }
            if (rounded > 255)
            {
                rounded = 255;
            }
            code = static_cast<int>(rounded);
        }
        else
        {
            const std::int64_t num = std::int64_t{255} * static_cast<std::int64_t>(i) +
                                     (static_cast<std::int64_t>(kLinearMax) / 2);
            code = static_cast<int>(num / kLinearMax);
        }
        lut[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(code);
    }
}

SiteKind siteKind(Cfa cfa, int px, int py)
{
    const CfaColor here = cfaColorAt(cfa, px, py);
    if (here == CfaColor::Red)
    {
        return SiteKind::Red;
    }
    if (here == CfaColor::Blue)
    {
        return SiteKind::Blue;
    }
    if (cfaColorAt(cfa, px + 1, py) == CfaColor::Red)
    {
        return SiteKind::GreenH;
    }
    return SiteKind::GreenV;
}

void rgbFromNeighbours(SiteKind kind, int center, int left, int right, int up, int down, int ul,
                       int ur, int dl, int dr, std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    const int orth = (left + right + up + down + 2) / 4;
    const int diag = (ul + ur + dl + dr + 2) / 4;
    const int horiz = (left + right + 1) / 2;
    const int vert = (up + down + 1) / 2;
    if (kind == SiteKind::Red)
    {
        r = static_cast<std::uint16_t>(center);
        g = static_cast<std::uint16_t>(orth);
        b = static_cast<std::uint16_t>(diag);
        return;
    }
    if (kind == SiteKind::Blue)
    {
        r = static_cast<std::uint16_t>(diag);
        g = static_cast<std::uint16_t>(orth);
        b = static_cast<std::uint16_t>(center);
        return;
    }
    if (kind == SiteKind::GreenH)
    {
        r = static_cast<std::uint16_t>(horiz);
        g = static_cast<std::uint16_t>(center);
        b = static_cast<std::uint16_t>(vert);
        return;
    }
    r = static_cast<std::uint16_t>(vert);
    g = static_cast<std::uint16_t>(center);
    b = static_cast<std::uint16_t>(horiz);
}

void storeEncoded(std::uint8_t *dst, const std::uint8_t *enc, std::uint16_t r, std::uint16_t g,
                  std::uint16_t b)
{
    dst[0] = enc[r];
    dst[1] = enc[g];
    dst[2] = enc[b];
}

int planeAt(const std::uint16_t *lin, int w, int x, int y)
{
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    return lin[index];
}

void sampleBorder(const std::uint16_t *lin, int w, int h, int x, int y, SiteKind kind,
                  std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    const int l = mirrorIndex(x - 1, w);
    const int rr = mirrorIndex(x + 1, w);
    const int u = mirrorIndex(y - 1, h);
    const int d = mirrorIndex(y + 1, h);
    rgbFromNeighbours(kind, planeAt(lin, w, x, y), planeAt(lin, w, l, y), planeAt(lin, w, rr, y),
                      planeAt(lin, w, x, u), planeAt(lin, w, x, d), planeAt(lin, w, l, u),
                      planeAt(lin, w, rr, u), planeAt(lin, w, l, d), planeAt(lin, w, rr, d), r, g,
                      b);
}

void emitInterior(const std::uint16_t *row, const std::uint16_t *up, const std::uint16_t *dn, int x,
                  SiteKind kind, std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    const int ix = x;
    rgbFromNeighbours(kind, row[ix], row[ix - 1], row[ix + 1], up[ix], dn[ix], up[ix - 1],
                      up[ix + 1], dn[ix - 1], dn[ix + 1], r, g, b);
}

void demosaicInteriorRows(const std::uint16_t *lin, int w, int h, const SiteKind kinds[4],
                          const std::uint8_t *enc, std::uint8_t *dst)
{
    if (w < 3 || h < 3)
    {
        return;
    }
    for (int y = 1; y < h - 1; ++y)
    {
        const std::size_t stride = static_cast<std::size_t>(w);
        const std::uint16_t *row = lin + static_cast<std::size_t>(y) * stride;
        const std::uint16_t *up = row - stride;
        const std::uint16_t *dn = row + stride;
        const int rowBase = (y & 1) << 1;
        const SiteKind evenKind = kinds[rowBase];
        const SiteKind oddKind = kinds[rowBase + 1];
        std::uint8_t *out = dst + static_cast<std::size_t>(y) * stride * 3U;
        for (int x = 2; x < w - 1; x += 2)
        {
            std::uint16_t r = 0;
            std::uint16_t g = 0;
            std::uint16_t b = 0;
            emitInterior(row, up, dn, x, evenKind, r, g, b);
            storeEncoded(out + static_cast<std::size_t>(x) * 3U, enc, r, g, b);
        }
        for (int x = 1; x < w - 1; x += 2)
        {
            std::uint16_t r = 0;
            std::uint16_t g = 0;
            std::uint16_t b = 0;
            emitInterior(row, up, dn, x, oddKind, r, g, b);
            storeEncoded(out + static_cast<std::size_t>(x) * 3U, enc, r, g, b);
        }
    }
}

void demosaicBorder(const std::uint16_t *lin, int w, int h, const SiteKind kinds[4],
                    const std::uint8_t *enc, std::uint8_t *dst)
{
    for (int y = 0; y < h; ++y)
    {
        const bool edgeRow = (y == 0) || (y == h - 1) || (w < 3);
        for (int x = 0; x < w; ++x)
        {
            if (!edgeRow && x > 0 && x < w - 1)
            {
                continue;
            }
            const int slot = ((y & 1) << 1) | (x & 1);
            std::uint16_t r = 0;
            std::uint16_t g = 0;
            std::uint16_t b = 0;
            sampleBorder(lin, w, h, x, y, kinds[slot], r, g, b);
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                       static_cast<std::size_t>(x)) *
                                      3U;
            storeEncoded(dst + index, enc, r, g, b);
        }
    }
}

void lineariseFrame(const std::vector<std::uint16_t> &samples, int w, int h,
                    const std::array<std::vector<std::uint16_t>, 4> &luts,
                    std::vector<std::uint16_t> &lin)
{
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    lin.resize(count);
    for (int y = 0; y < h; ++y)
    {
        const int rowSlot = (y & 1) << 1;
        const auto &evenLut = luts[static_cast<std::size_t>(rowSlot)];
        const auto &oddLut = luts[static_cast<std::size_t>(rowSlot + 1)];
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            const std::uint16_t sample = samples[row + static_cast<std::size_t>(x)];
            const auto &table = ((x & 1) == 0) ? evenLut : oddLut;
            lin[row + static_cast<std::size_t>(x)] = lookupSample(table, sample);
        }
    }
}

void encodeMono(const std::vector<std::uint16_t> &lin, const std::uint8_t *enc, std::uint8_t *dst)
{
    const std::size_t n = lin.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint8_t v = enc[lin[i]];
        std::uint8_t *pixel = dst + i * 3U;
        pixel[0] = v;
        pixel[1] = v;
        pixel[2] = v;
    }
}

bool previewDimensions(int w, int h, int maxEdge, int &outW, int &outH)
{
    outW = w;
    outH = h;
    if (maxEdge <= 0)
    {
        return false;
    }
    const int longest = (w >= h) ? w : h;
    if (longest <= maxEdge)
    {
        return false;
    }
    if (w >= h)
    {
        outW = maxEdge;
        outH = static_cast<int>(static_cast<std::int64_t>(h) * static_cast<std::int64_t>(maxEdge) /
                                static_cast<std::int64_t>(w));
    }
    else
    {
        outH = maxEdge;
        outW = static_cast<int>(static_cast<std::int64_t>(w) * static_cast<std::int64_t>(maxEdge) /
                                static_cast<std::int64_t>(h));
    }
    if (outW < 1)
    {
        outW = 1;
    }
    if (outH < 1)
    {
        outH = 1;
    }
    return true;
}

std::uint16_t quadSample(const std::vector<std::uint16_t> &samples, int w,
                         const std::array<std::vector<std::uint16_t>, 4> &luts, int x, int y)
{
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    const int slot = ((y & 1) << 1) | (x & 1);
    return lookupSample(luts[static_cast<std::size_t>(slot)], samples[index]);
}

void colourFromQuad(const std::vector<std::uint16_t> &samples, int w, int h, Cfa cfa,
                    const std::array<std::vector<std::uint16_t>, 4> &luts, int sx, int sy,
                    std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    if (sx < 0)
    {
        sx = 0;
    }
    if (sy < 0)
    {
        sy = 0;
    }
    if (sx >= w)
    {
        sx = w - 1;
    }
    if (sy >= h)
    {
        sy = h - 1;
    }
    if (w < 2 || h < 2 || cfa == Cfa::Mono)
    {
        const std::uint16_t v = quadSample(samples, w, luts, sx, sy);
        r = v;
        g = v;
        b = v;
        return;
    }
    int qx = sx & ~1;
    int qy = sy & ~1;
    const int maxX = (w - 2) & ~1;
    const int maxY = (h - 2) & ~1;
    if (qx > maxX)
    {
        qx = maxX;
    }
    if (qy > maxY)
    {
        qy = maxY;
    }
    if (qx < 0)
    {
        qx = 0;
    }
    if (qy < 0)
    {
        qy = 0;
    }
    std::uint16_t red = 0;
    std::uint16_t blue = 0;
    int gSum = 0;
    int gCount = 0;
    for (int dy = 0; dy < 2; ++dy)
    {
        for (int dx = 0; dx < 2; ++dx)
        {
            const std::uint16_t v = quadSample(samples, w, luts, qx + dx, qy + dy);
            const CfaColor colour = cfaColorAt(cfa, qx + dx, qy + dy);
            if (colour == CfaColor::Red)
            {
                red = v;
            }
            else if (colour == CfaColor::Blue)
            {
                blue = v;
            }
            else
            {
                gSum += v;
                ++gCount;
            }
        }
    }
    r = red;
    b = blue;
    g = static_cast<std::uint16_t>(gCount > 0 ? (gSum + gCount / 2) / gCount : 0);
}

void renderPreview(const Params &p, const std::vector<std::uint16_t> &samples,
                   const std::array<std::vector<std::uint16_t>, 4> &luts, const std::uint8_t *enc,
                   int outW, int outH, std::uint8_t *dst)
{
    const int w = p.width;
    const int h = p.height;
    for (int oy = 0; oy < outH; ++oy)
    {
        const int sy =
            static_cast<int>(static_cast<std::int64_t>(oy) * static_cast<std::int64_t>(h) /
                             static_cast<std::int64_t>(outH));
        std::uint8_t *row =
            dst + static_cast<std::size_t>(oy) * static_cast<std::size_t>(outW) * 3U;
        for (int ox = 0; ox < outW; ++ox)
        {
            const int sx =
                static_cast<int>(static_cast<std::int64_t>(ox) * static_cast<std::int64_t>(w) /
                                 static_cast<std::int64_t>(outW));
            std::uint16_t r = 0;
            std::uint16_t g = 0;
            std::uint16_t b = 0;
            colourFromQuad(samples, w, h, p.cfa, luts, sx, sy, r, g, b);
            storeEncoded(row + static_cast<std::size_t>(ox) * 3U, enc, r, g, b);
        }
    }
}

ImageData allocateRgb(int w, int h)
{
    ImageData image;
    if (w < 1 || h < 1)
    {
        return image;
    }
    const std::size_t sw = static_cast<std::size_t>(w);
    const std::size_t sh = static_cast<std::size_t>(h);
    if (sw != 0 && sh > (static_cast<std::size_t>(-1) / 3U) / sw)
    {
        return image;
    }
    const std::size_t bytes = sw * sh * 3U;
    image.buffer = std::make_shared<std::vector<std::uint8_t>>(bytes);
    image.width = w;
    image.height = h;
    image.format = PixelFormat::RGB24;
    return image;
}

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
    const int w = p.width;
    const int h = p.height;
    for (int y = 0; y < h; ++y)
    {
        const int c0 = colourIndex(cfaColorAt(p.cfa, 0, y));
        const int c1 = colourIndex(cfaColorAt(p.cfa, 1, y));
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            const std::uint16_t sample = samples[row + static_cast<std::size_t>(x)];
            if (static_cast<double>(sample) >= clipAt)
            {
                continue;
            }
            int value = static_cast<int>(sample) - p.blackLevel;
            if (value < 0)
            {
                value = 0;
            }
            if (value >= levels)
            {
                value = levels - 1;
            }
            const int channel = ((x & 1) == 0) ? c0 : c1;
            hist[static_cast<std::size_t>(channel)][static_cast<std::size_t>(value)] += 1U;
        }
    }
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

CfaColor cfaColorAt(Cfa cfa, int x, int y)
{
    if (cfa == Cfa::Mono)
    {
        return CfaColor::Green;
    }
    static constexpr CfaColor kPattern[4][4] = {
        {CfaColor::Red, CfaColor::Green, CfaColor::Green, CfaColor::Blue},
        {CfaColor::Blue, CfaColor::Green, CfaColor::Green, CfaColor::Red},
        {CfaColor::Green, CfaColor::Red, CfaColor::Blue, CfaColor::Green},
        {CfaColor::Green, CfaColor::Blue, CfaColor::Red, CfaColor::Green},
    };
    const int slot = ((y & 1) << 1) | (x & 1);
    const int which = static_cast<int>(cfa);
    if (which < 0 || which > 3)
    {
        return CfaColor::Green;
    }
    return kPattern[which][slot];
}

double srgbEncode(double linear)
{
    if (!(linear > 0.0))
    {
        return 0.0;
    }
    if (linear >= 1.0)
    {
        return 1.0;
    }
    if (linear <= 0.0031308)
    {
        return 12.92 * linear;
    }
    return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

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

ImageData renderPlainRawWithGains(const Params &p, const std::vector<std::uint16_t> &samples,
                                  const WbGains &gains, int maxEdge)
{
    if (!inputOk(p, samples))
    {
        return {};
    }
    const int white = effectiveWhiteLevel(p);
    std::array<std::vector<std::uint16_t>, 4> luts;
    buildPositionLuts(p, gains, white, luts);
    std::vector<std::uint8_t> encode;
    buildEncodeLut(p.srgbGamma, encode);
    int outW = 0;
    int outH = 0;
    const bool preview = previewDimensions(p.width, p.height, maxEdge, outW, outH);
    ImageData image = allocateRgb(preview ? outW : p.width, preview ? outH : p.height);
    if (image.isNull() || !image.buffer)
    {
        return {};
    }
    std::uint8_t *dst = image.buffer->data();
    if (preview)
    {
        renderPreview(p, samples, luts, encode.data(), image.width, image.height, dst);
        return image;
    }
    std::vector<std::uint16_t> lin;
    lineariseFrame(samples, p.width, p.height, luts, lin);
    if (p.cfa == Cfa::Mono)
    {
        encodeMono(lin, encode.data(), dst);
        return image;
    }
    SiteKind kinds[4] = {siteKind(p.cfa, 0, 0), siteKind(p.cfa, 1, 0), siteKind(p.cfa, 0, 1),
                         siteKind(p.cfa, 1, 1)};
    demosaicInteriorRows(lin.data(), p.width, p.height, kinds, encode.data(), dst);
    demosaicBorder(lin.data(), p.width, p.height, kinds, encode.data(), dst);
    return image;
}

ImageData renderPlainRaw(const Params &p, const std::vector<std::uint16_t> &samples, int maxEdge)
{
    if (!inputOk(p, samples))
    {
        return {};
    }
    const WbGains gains = computeWhiteBalance(p, samples);
    return renderPlainRawWithGains(p, samples, gains, maxEdge);
}

} // namespace mviewer::core::rawplain
