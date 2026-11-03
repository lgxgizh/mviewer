#include "core/image/yuv/YuvToRgb.h"

#include <algorithm>
#include <cmath>

namespace mviewer::core::yuv
{
namespace
{

struct KrKb
{
    double kr;
    double kb;
};

KrKb lumaWeights(Matrix m)
{
    switch (m)
    {
    case Matrix::BT601:
        return {0.299, 0.114};
    case Matrix::BT2020:
        return {0.2627, 0.0593};
    case Matrix::BT709:
    default:
        return {0.2126, 0.0722};
    }
}

int32_t q16(double v)
{
    return static_cast<int32_t>(std::lround(65536.0 * v));
}

uint8_t clamp8(int32_t v)
{
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

// Expected sizes of one plane; false when the vector is too short.
bool planeOk(const std::vector<uint16_t> &plane, int w, int h)
{
    return w > 0 && h > 0 && plane.size() >= static_cast<size_t>(w) * static_cast<size_t>(h);
}

bool planesMatch(const YuvPlanes &p, const Format &f)
{
    if (!isValid(f) || p.width != f.width || p.height != f.height || p.bitDepth != f.bitDepth)
        return false;
    if (!planeOk(p.y, p.width, p.height))
        return false;
    if (f.subsampling == Subsampling::S400)
        return true;
    return p.chromaWidth == chromaWidth(f) && p.chromaHeight == chromaHeight(f) &&
           planeOk(p.u, p.chromaWidth, p.chromaHeight) &&
           planeOk(p.v, p.chromaWidth, p.chromaHeight);
}

// Weighted pick of two chroma neighbours for one output position along an axis.
// 'cosited': chroma sits on even luma positions; otherwise it sits between them (3:1).
int16_t upsampleAxis(const int16_t *c, int count, int pos, bool cosited)
{
    const int base = pos >> 1;
    const int last = count - 1;
    const int a = std::min(base, last);
    if (cosited)
    {
        if ((pos & 1) == 0)
            return c[a];
        const int b = std::min(base + 1, last);
        return static_cast<int16_t>((c[a] + c[b] + 1) >> 1);
    }
    const int b = (pos & 1) == 0 ? std::max(base - 1, 0) : std::min(base + 1, last);
    return static_cast<int16_t>((3 * c[a] + c[b] + 2) >> 2);
}

struct ChromaRowContext
{
    const YuvPlanes *planes;
    ChromaUpsample up;
    int shiftX;
    int shiftY;
    bool cositedX;
    bool cositedY;
};

// Builds one 12-bit chroma plane row (subsampled width) for luma row 'y'.
void chromaSourceRow(const ChromaRowContext &ctx, const std::vector<uint16_t> &plane, int y,
                     std::vector<int16_t> &out)
{
    const int cw = ctx.planes->chromaWidth;
    const int ch = ctx.planes->chromaHeight;
    const int depth = ctx.planes->bitDepth;
    out.resize(static_cast<size_t>(cw));
    auto rowPtr = [&](int cy)
    { return plane.data() + static_cast<size_t>(cy) * static_cast<size_t>(cw); };
    if (ctx.shiftY == 0 || ctx.up == ChromaUpsample::Nearest)
    {
        const uint16_t *r = rowPtr(std::min(y >> ctx.shiftY, ch - 1));
        for (int x = 0; x < cw; ++x)
            out[static_cast<size_t>(x)] = toTwelveBit(r[x], depth);
        return;
    }
    const int base = std::min(y >> 1, ch - 1);
    const int next = std::min(base + 1, ch - 1);
    const int prev = ctx.cositedY ? base : std::max(base - 1, 0);
    const int other = (y & 1) == 0 ? prev : next;
    const uint16_t *ra = rowPtr(base);
    const uint16_t *rb = rowPtr(other);
    for (int x = 0; x < cw; ++x)
    {
        const int a = toTwelveBit(ra[x], depth);
        const int b = toTwelveBit(rb[x], depth);
        const int v = ctx.cositedY ? ((a + b + 1) >> 1) : ((3 * a + b + 2) >> 2);
        out[static_cast<size_t>(x)] = static_cast<int16_t>(v);
    }
}

// Expands a subsampled 12-bit chroma row to luma width.
void expandRow(const ChromaRowContext &ctx, const std::vector<int16_t> &src, int width,
               int16_t *dst)
{
    const int cw = static_cast<int>(src.size());
    if (ctx.shiftX == 0)
    {
        std::copy(src.begin(), src.begin() + std::min(cw, width), dst);
        return;
    }
    for (int x = 0; x < width; ++x)
    {
        if (ctx.up == ChromaUpsample::Nearest)
            dst[x] = src[static_cast<size_t>(std::min(x >> 1, cw - 1))];
        else
            dst[x] = upsampleAxis(src.data(), cw, x, ctx.cositedX);
    }
}

struct RowBuffers
{
    std::vector<int16_t> y;
    std::vector<int16_t> u;
    std::vector<int16_t> v;
    std::vector<int16_t> srcU;
    std::vector<int16_t> srcV;
};

} // namespace

YuvCoeffs makeYuvCoeffs(Matrix m, Range r, int bitDepth)
{
    const bool full = r == Range::Full;
    const int depth = std::clamp(bitDepth, 1, 16);
    const double fullMax = depth >= 12 ? 4095.0 : double(((1 << depth) - 1) << (12 - depth));
    const double yRange = full ? fullMax : 3504.0;
    const double cRange = full ? fullMax : 3584.0;
    const KrKb k = lumaWeights(m);
    const double kg = 1.0 - k.kr - k.kb;
    YuvCoeffs c;
    c.yOff = full ? 0 : 256;
    c.cOff = 2048;
    c.cy = q16(255.0 / yRange);
    c.crv = q16(255.0 * 2.0 * (1.0 - k.kr) / cRange);
    c.cbu = q16(255.0 * 2.0 * (1.0 - k.kb) / cRange);
    c.cgu = q16(255.0 * 2.0 * k.kb * (1.0 - k.kb) / kg / cRange);
    c.cgv = q16(255.0 * 2.0 * k.kr * (1.0 - k.kr) / kg / cRange);
    return c;
}

int16_t toTwelveBit(uint16_t sample, int bitDepth)
{
    const int depth = std::clamp(bitDepth, 1, 16);
    const int v = depth >= 12 ? (sample >> (depth - 12)) : (sample << (12 - depth));
    return static_cast<int16_t>(std::min(v, 4095));
}

void convertRowScalar(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                      const YuvCoeffs &c, uint8_t *rgb24)
{
    const bool gray = u12 == nullptr || v12 == nullptr;
    for (int i = 0; i < n; ++i)
    {
        const int32_t yy = c.cy * (y12[i] - c.yOff) + 32768;
        uint8_t *px = rgb24 + static_cast<ptrdiff_t>(i) * 3;
        if (gray)
        {
            px[0] = px[1] = px[2] = clamp8(yy >> 16);
            continue;
        }
        const int32_t u = u12[i] - c.cOff;
        const int32_t v = v12[i] - c.cOff;
        px[0] = clamp8((yy + c.crv * v) >> 16);
        px[1] = clamp8((yy - c.cgu * u - c.cgv * v) >> 16);
        px[2] = clamp8((yy + c.cbu * u) >> 16);
    }
}

RowConverterFn selectRowConverter()
{
    return rowConverterFor(bestRowConverterKind());
}

bool convertToRgb24(const YuvPlanes &p, const Format &f, ChromaUpsample up, uint8_t *dst,
                    size_t dstSize, size_t dstStride)
{
    if (dst == nullptr || !planesMatch(p, f))
        return false;
    const auto w = static_cast<size_t>(p.width);
    const auto h = static_cast<size_t>(p.height);
    if (dstStride < w * 3 || dstSize < dstStride * (h - 1) + w * 3)
        return false;
    const YuvCoeffs c = makeYuvCoeffs(f.matrix, f.range, f.bitDepth);
    const RowConverterFn convert = selectRowConverter();
    const bool gray = f.subsampling == Subsampling::S400;
    const ChromaRowContext ctx{&p,
                               up,
                               chromaShiftX(f.subsampling),
                               chromaShiftY(f.subsampling),
                               f.siting != ChromaSiting::Center,
                               f.siting == ChromaSiting::TopLeft};
    RowBuffers b;
    b.y.resize(w);
    b.u.resize(gray ? 0 : w);
    b.v.resize(gray ? 0 : w);
    for (size_t row = 0; row < h; ++row)
    {
        const uint16_t *ys = p.y.data() + row * w;
        for (size_t x = 0; x < w; ++x)
            b.y[x] = toTwelveBit(ys[x], p.bitDepth);
        if (!gray)
        {
            chromaSourceRow(ctx, p.u, static_cast<int>(row), b.srcU);
            chromaSourceRow(ctx, p.v, static_cast<int>(row), b.srcV);
            expandRow(ctx, b.srcU, p.width, b.u.data());
            expandRow(ctx, b.srcV, p.width, b.v.data());
        }
        convert(b.y.data(), gray ? nullptr : b.u.data(), gray ? nullptr : b.v.data(), p.width, c,
                dst + row * dstStride);
    }
    return true;
}

std::vector<uint8_t> toRgb24(const YuvPlanes &p, const Format &f, ChromaUpsample up)
{
    if (p.width <= 0 || p.height <= 0)
        return {};
    const size_t stride = static_cast<size_t>(p.width) * 3;
    std::vector<uint8_t> out(stride * static_cast<size_t>(p.height));
    if (!convertToRgb24(p, f, up, out.data(), out.size(), stride))
        return {};
    return out;
}

} // namespace mviewer::core::yuv
