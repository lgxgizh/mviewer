#include "core/image/rawplain/PlainRawRender.h"

#include "core/image/rawplain/PlainRawPreview.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
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

constexpr int kLinearMax = 65535;

std::atomic<PlainRawRowsRunner> &plainRawRowsSlot()
{
    static std::atomic<PlainRawRowsRunner> slot{nullptr};
    return slot;
}

void dispatchRows(int rowCount, const std::function<void(int, int)> &fn)
{
    if (rowCount <= 0)
    {
        return;
    }
    const PlainRawRowsRunner runner = plainRawRowsSlot().load(std::memory_order_acquire);
    if (runner == nullptr)
    {
        fn(0, rowCount);
        return;
    }
    runner(rowCount, fn);
}

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

void demosaicInteriorBand(const std::uint16_t *lin, int w, int h, int y0, int y1,
                          const SiteKind kinds[4], const std::uint8_t *enc, std::uint8_t *dst)
{
    int begin = y0 < 1 ? 1 : y0;
    int end = y1 > h - 1 ? h - 1 : y1;
    for (int y = begin; y < end; ++y)
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

void demosaicInteriorRows(const std::uint16_t *lin, int w, int h, const SiteKind kinds[4],
                          const std::uint8_t *enc, std::uint8_t *dst)
{
    if (w < 3 || h < 3)
    {
        return;
    }
    dispatchRows(h,
                 [&](int y0, int y1) { demosaicInteriorBand(lin, w, h, y0, y1, kinds, enc, dst); });
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

void lineariseBand(std::uint16_t *samples, int w, int y0, int y1,
                   const std::array<std::vector<std::uint16_t>, 4> &luts)
{
    for (int y = y0; y < y1; ++y)
    {
        const int rowSlot = (y & 1) << 1;
        const auto &evenLut = luts[static_cast<std::size_t>(rowSlot)];
        const auto &oddLut = luts[static_cast<std::size_t>(rowSlot) + 1U];
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            std::uint16_t &sample = samples[row + static_cast<std::size_t>(x)];
            const auto &table = ((x & 1) == 0) ? evenLut : oddLut;
            sample = lookupSample(table, sample);
        }
    }
}

void lineariseInPlace(std::uint16_t *samples, int w, int h,
                      const std::array<std::vector<std::uint16_t>, 4> &luts)
{
    if (samples == nullptr || w < 1 || h < 1)
    {
        return;
    }
    dispatchRows(h, [&](int y0, int y1) { lineariseBand(samples, w, y0, y1, luts); });
}

void encodeMonoBand(const std::uint16_t *lin, int w, int y0, int y1, const std::uint8_t *enc,
                    std::uint8_t *dst)
{
    for (int y = y0; y < y1; ++y)
    {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        std::uint8_t *out = dst + row * 3U;
        for (int x = 0; x < w; ++x)
        {
            const std::uint8_t v = enc[lin[row + static_cast<std::size_t>(x)]];
            std::uint8_t *pixel = out + static_cast<std::size_t>(x) * 3U;
            pixel[0] = v;
            pixel[1] = v;
            pixel[2] = v;
        }
    }
}

void encodeMono(const std::uint16_t *lin, int w, int h, const std::uint8_t *enc, std::uint8_t *dst)
{
    if (lin == nullptr || w < 1 || h < 1)
    {
        return;
    }
    dispatchRows(h, [&](int y0, int y1) { encodeMonoBand(lin, w, y0, y1, enc, dst); });
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

namespace
{

ImageData renderPrepared(const Params &p, std::vector<std::uint16_t> samples, const WbGains &gains,
                         int maxEdge)
{
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
        detail::renderPreview(p, samples, luts, encode.data(), image.width, image.height, dst);
        return image;
    }
    lineariseInPlace(samples.data(), p.width, p.height, luts);
    if (p.cfa == Cfa::Mono)
    {
        encodeMono(samples.data(), p.width, p.height, encode.data(), dst);
        return image;
    }
    SiteKind kinds[4] = {siteKind(p.cfa, 0, 0), siteKind(p.cfa, 1, 0), siteKind(p.cfa, 0, 1),
                         siteKind(p.cfa, 1, 1)};
    demosaicInteriorRows(samples.data(), p.width, p.height, kinds, encode.data(), dst);
    demosaicBorder(samples.data(), p.width, p.height, kinds, encode.data(), dst);
    return image;
}

} // namespace

void setPlainRawRowsRunner(PlainRawRowsRunner runner) noexcept
{
    plainRawRowsSlot().store(runner, std::memory_order_release);
}

ImageData renderPlainRawWithGains(const Params &p, const std::vector<std::uint16_t> &samples,
                                  const WbGains &gains, int maxEdge)
{
    if (!inputOk(p, samples))
    {
        return {};
    }
    return renderPrepared(p, std::vector<std::uint16_t>(samples), gains, maxEdge);
}

ImageData renderPlainRawOwned(const Params &p, std::vector<std::uint16_t> &&samples, int maxEdge)
{
    if (!inputOk(p, samples))
    {
        return {};
    }
    const WbGains gains = computeWhiteBalance(p, samples);
    return renderPrepared(p, std::move(samples), gains, maxEdge);
}

ImageData renderPlainRaw(const Params &p, const std::vector<std::uint16_t> &samples, int maxEdge)
{
    if (!inputOk(p, samples))
    {
        return {};
    }
    return renderPlainRawOwned(p, std::vector<std::uint16_t>(samples), maxEdge);
}

} // namespace mviewer::core::rawplain
