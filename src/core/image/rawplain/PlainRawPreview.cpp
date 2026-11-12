#include "core/image/rawplain/PlainRawPreview.h"

#include "core/image/rawplain/PlainRawRender.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mviewer::core::rawplain
{
namespace
{

constexpr int kLinearMax = 65535;

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

void storeEncoded(std::uint8_t *dst, const std::uint8_t *enc, std::uint16_t r, std::uint16_t g,
                  std::uint16_t b)
{
    dst[0] = enc[r];
    dst[1] = enc[g];
    dst[2] = enc[b];
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

std::uint16_t meanCode(std::int64_t sum, int count)
{
    if (count < 1)
    {
        return 0;
    }
    std::int64_t value = (sum + count / 2) / count;
    if (value < 0)
    {
        value = 0;
    }
    if (value > kLinearMax)
    {
        value = kLinearMax;
    }
    return static_cast<std::uint16_t>(value);
}

int gridCount(int span, int cap)
{
    int n = span < 1 ? 1 : span;
    if (n > cap)
    {
        n = cap;
    }
    return n;
}

int gridPos(int start, int span, int index, int count)
{
    if (count <= 1)
    {
        return start;
    }
    const auto pos = static_cast<std::int64_t>(start) +
                     (static_cast<std::int64_t>(index) * static_cast<std::int64_t>(span)) / count;
    if (pos < 0)
    {
        return 0;
    }
    return static_cast<int>(pos);
}

int clampTo(int v, int limit)
{
    if (v < 0 || limit < 1)
    {
        return 0;
    }
    if (v >= limit)
    {
        return limit - 1;
    }
    return v;
}

void spanOf(int index, int count, int src, int &start, int &span)
{
    const auto a = static_cast<std::int64_t>(index) * static_cast<std::int64_t>(src) / count;
    const auto b = static_cast<std::int64_t>(index + 1) * static_cast<std::int64_t>(src) / count;
    start = static_cast<int>(a);
    span = static_cast<int>(b - a);
    if (span < 1)
    {
        span = 1;
    }
}

void averageMono(const std::vector<std::uint16_t> &samples, int w, int h,
                 const std::array<std::vector<std::uint16_t>, 4> &luts, int x0, int spanX, int y0,
                 int spanY, std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    const int nx = gridCount(spanX, 8);
    const int ny = gridCount(spanY, 8);
    std::int64_t sum = 0;
    int n = 0;
    for (int iy = 0; iy < ny; ++iy)
    {
        const int sy = clampTo(gridPos(y0, spanY, iy, ny), h);
        for (int ix = 0; ix < nx; ++ix)
        {
            const int sx = clampTo(gridPos(x0, spanX, ix, nx), w);
            sum += quadSample(samples, w, luts, sx, sy);
            ++n;
        }
    }
    const std::uint16_t v = meanCode(sum, n);
    r = v;
    g = v;
    b = v;
}

void averageBayer(const std::vector<std::uint16_t> &samples, int w, int h, Cfa cfa,
                  const std::array<std::vector<std::uint16_t>, 4> &luts, int x0, int spanX, int y0,
                  int spanY, std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    const int nx = gridCount(spanX / 2, 4);
    const int ny = gridCount(spanY / 2, 4);
    std::int64_t rSum = 0;
    std::int64_t gSum = 0;
    std::int64_t bSum = 0;
    int n = 0;
    for (int iy = 0; iy < ny; ++iy)
    {
        const int sy = gridPos(y0, spanY, iy, ny) & ~1;
        for (int ix = 0; ix < nx; ++ix)
        {
            const int sx = gridPos(x0, spanX, ix, nx) & ~1;
            std::uint16_t qr = 0;
            std::uint16_t qg = 0;
            std::uint16_t qb = 0;
            colourFromQuad(samples, w, h, cfa, luts, sx < 0 ? 0 : sx, sy < 0 ? 0 : sy, qr, qg, qb);
            rSum += qr;
            gSum += qg;
            bSum += qb;
            ++n;
        }
    }
    r = meanCode(rSum, n);
    g = meanCode(gSum, n);
    b = meanCode(bSum, n);
}

void averageFootprint(const Params &p, const std::vector<std::uint16_t> &samples,
                      const std::array<std::vector<std::uint16_t>, 4> &luts, int ox, int oy,
                      int outW, int outH, std::uint16_t &r, std::uint16_t &g, std::uint16_t &b)
{
    int x0 = 0;
    int y0 = 0;
    int spanX = 1;
    int spanY = 1;
    spanOf(ox, outW, p.width, x0, spanX);
    spanOf(oy, outH, p.height, y0, spanY);
    if (p.cfa == Cfa::Mono || p.width < 2 || p.height < 2)
    {
        averageMono(samples, p.width, p.height, luts, x0, spanX, y0, spanY, r, g, b);
        return;
    }
    averageBayer(samples, p.width, p.height, p.cfa, luts, x0, spanX, y0, spanY, r, g, b);
}

} // namespace

namespace detail
{

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

void renderPreview(const Params &p, const std::vector<std::uint16_t> &samples,
                   const std::array<std::vector<std::uint16_t>, 4> &luts, const std::uint8_t *enc,
                   int outW, int outH, std::uint8_t *dst)
{
    for (int oy = 0; oy < outH; ++oy)
    {
        std::uint8_t *row =
            dst + static_cast<std::size_t>(oy) * static_cast<std::size_t>(outW) * 3U;
        for (int ox = 0; ox < outW; ++ox)
        {
            std::uint16_t r = 0;
            std::uint16_t g = 0;
            std::uint16_t b = 0;
            averageFootprint(p, samples, luts, ox, oy, outW, outH, r, g, b);
            storeEncoded(row + static_cast<std::size_t>(ox) * 3U, enc, r, g, b);
        }
    }
}

} // namespace detail
} // namespace mviewer::core::rawplain
