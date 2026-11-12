#include "core/image/yuv/YuvToRgb.h"

#include <algorithm>
#include <atomic>
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
        // SDR Y'CbCr only: BT.2020 Kr/Kb. No PQ/HLG transfer, no HDR tone map.
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

// Normalizes one row of samples to 12 bits (same result as toTwelveBit per sample).
// The shift is uniform across the row, so the loop vectorizes.
void toTwelveBitRow(const uint16_t *src, int n, int bitDepth, int16_t *dst)
{
    const int depth = std::clamp(bitDepth, 1, 16);
    if (depth >= 12)
    {
        const int shift = depth - 12;
        for (int x = 0; x < n; ++x)
            dst[x] = static_cast<int16_t>(std::min(static_cast<int>(src[x]) >> shift, 4095));
        return;
    }
    const int shift = 12 - depth;
    for (int x = 0; x < n; ++x)
        dst[x] = static_cast<int16_t>(std::min(static_cast<int>(src[x]) << shift, 4095));
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

// Chroma plane rows (a, b) and blend mode that produce the chroma row for luma row 'y'.
// Rows with equal taps produce identical chroma, so the caller reuses them.
struct ChromaTaps
{
    int a = 0;
    int b = 0;
    bool blend = false;

    bool operator==(const ChromaTaps &) const = default;
};

ChromaTaps chromaTapsFor(const ChromaRowContext &ctx, int y)
{
    const int ch = ctx.planes->chromaHeight;
    ChromaTaps t;
    if (ctx.shiftY == 0 || ctx.up == ChromaUpsample::Nearest)
    {
        t.a = std::min(y >> ctx.shiftY, ch - 1);
        t.b = t.a;
        return t;
    }
    const int base = std::min(y >> 1, ch - 1);
    const int next = std::min(base + 1, ch - 1);
    const int prev = ctx.cositedY ? base : std::max(base - 1, 0);
    t.a = base;
    t.b = (y & 1) == 0 ? prev : next;
    t.blend = true;
    return t;
}

// Builds one 12-bit chroma plane row (subsampled width) from the taps.
void chromaSourceRow(const ChromaRowContext &ctx, const std::vector<uint16_t> &plane,
                     const ChromaTaps &t, std::vector<int16_t> &out)
{
    const int cw = ctx.planes->chromaWidth;
    const int depth = ctx.planes->bitDepth;
    out.resize(static_cast<size_t>(cw));
    auto rowPtr = [&](int cy)
    { return plane.data() + static_cast<size_t>(cy) * static_cast<size_t>(cw); };
    if (!t.blend)
    {
        toTwelveBitRow(rowPtr(t.a), cw, depth, out.data());
        return;
    }
    const uint16_t *ra = rowPtr(t.a);
    const uint16_t *rb = rowPtr(t.b);
    for (int x = 0; x < cw; ++x)
    {
        const int a = toTwelveBit(ra[x], depth);
        const int b = toTwelveBit(rb[x], depth);
        const int v = ctx.cositedY ? ((a + b + 1) >> 1) : ((3 * a + b + 2) >> 2);
        out[static_cast<size_t>(x)] = static_cast<int16_t>(v);
    }
}

// Expands a subsampled 12-bit chroma row to luma width (2:1 horizontally when shiftX
// is 1). Bilinear: cosited chroma sits on even luma positions (odd ones average the
// two neighbours); centered chroma weights the nearer neighbour 3:1. Edges clamp.
void expandRow(const ChromaRowContext &ctx, const std::vector<int16_t> &src, int width,
               int16_t *dst)
{
    const int cw = static_cast<int>(src.size());
    if (ctx.shiftX == 0)
    {
        std::copy(src.begin(), src.begin() + std::min(cw, width), dst);
        return;
    }
    const int16_t *c = src.data();
    const int last = cw - 1;
    if (ctx.up == ChromaUpsample::Nearest)
    {
        for (int k = 0; k < cw; ++k)
        {
            const int x = 2 * k;
            if (x < width)
                dst[x] = c[k];
            if (x + 1 < width)
                dst[x + 1] = c[k];
        }
        return;
    }
    for (int k = 0; k < cw; ++k)
    {
        const int x = 2 * k;
        const int a = c[k];
        const int nextC = c[std::min(k + 1, last)];
        if (ctx.cositedX)
        {
            if (x < width)
                dst[x] = static_cast<int16_t>(a);
            if (x + 1 < width)
                dst[x + 1] = static_cast<int16_t>((a + nextC + 1) >> 1);
            continue;
        }
        const int prevC = c[std::max(k - 1, 0)];
        if (x < width)
            dst[x] = static_cast<int16_t>((3 * a + prevC + 2) >> 2);
        if (x + 1 < width)
            dst[x + 1] = static_cast<int16_t>((3 * a + nextC + 2) >> 2);
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

struct FrameJob
{
    const YuvPlanes *p;
    YuvCoeffs c;
    RowConverterFn convert;
    bool gray;
    ChromaRowContext ctx;
    uint8_t *dst;
    size_t dstStride;
};

// Converts luma rows [y0, y1). Each call owns its scratch, so bands run concurrently.
void convertRows(const FrameJob &job, int y0, int y1)
{
    const YuvPlanes &p = *job.p;
    const auto w = static_cast<size_t>(p.width);
    RowBuffers b;
    b.y.resize(w);
    b.u.resize(job.gray ? 0 : w);
    b.v.resize(job.gray ? 0 : w);
    ChromaTaps lastTaps;
    bool haveChroma = false;
    for (int row = y0; row < y1; ++row)
    {
        const size_t r = static_cast<size_t>(row);
        toTwelveBitRow(p.y.data() + r * w, p.width, p.bitDepth, b.y.data());
        if (!job.gray)
        {
            const ChromaTaps taps = chromaTapsFor(job.ctx, row);
            if (!haveChroma || !(taps == lastTaps))
            {
                chromaSourceRow(job.ctx, p.u, taps, b.srcU);
                chromaSourceRow(job.ctx, p.v, taps, b.srcV);
                expandRow(job.ctx, b.srcU, p.width, b.u.data());
                expandRow(job.ctx, b.srcV, p.width, b.v.data());
                lastTaps = taps;
                haveChroma = true;
            }
        }
        job.convert(b.y.data(), job.gray ? nullptr : b.u.data(), job.gray ? nullptr : b.v.data(),
                    p.width, job.c, job.dst + r * job.dstStride);
    }
}

std::atomic<YuvRowsRunner> g_rowsRunner{nullptr};
std::atomic<std::uint8_t> g_defaultUpsample{static_cast<std::uint8_t>(ChromaUpsample::Bilinear)};

// Frames below this many pixels convert on the calling thread.
constexpr int64_t kParallelMinPixels = int64_t{1} << 20;
constexpr int kParallelMinRows = 64;

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

void setYuvRowsRunner(YuvRowsRunner runner) noexcept
{
    g_rowsRunner.store(runner, std::memory_order_release);
}

YuvRowsRunner yuvRowsRunner() noexcept
{
    return g_rowsRunner.load(std::memory_order_acquire);
}

ChromaUpsample defaultChromaUpsample() noexcept
{
    const auto raw = g_defaultUpsample.load(std::memory_order_acquire);
    return static_cast<ChromaUpsample>(raw);
}

void setDefaultChromaUpsample(ChromaUpsample up) noexcept
{
    g_defaultUpsample.store(static_cast<std::uint8_t>(up), std::memory_order_release);
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
    const bool gray = f.subsampling == Subsampling::S400;
    const FrameJob job{
        &p,
        makeYuvCoeffs(f.matrix, f.range, f.bitDepth),
        selectRowConverter(),
        gray,
        ChromaRowContext{&p, up, chromaShiftX(f.subsampling), chromaShiftY(f.subsampling),
                         f.siting != ChromaSiting::Center, f.siting == ChromaSiting::TopLeft},
        dst,
        dstStride};
    const YuvRowsRunner runner = yuvRowsRunner();
    const int64_t pixels = static_cast<int64_t>(p.width) * static_cast<int64_t>(p.height);
    if (runner == nullptr || pixels < kParallelMinPixels || p.height < kParallelMinRows)
    {
        convertRows(job, 0, p.height);
        return true;
    }
    std::atomic<bool> failed{false};
    runner(p.height,
           [&job, &failed](int y0, int y1)
           {
               try
               {
                   convertRows(job, y0, y1);
               }
               catch (...)
               {
                   failed.store(true, std::memory_order_relaxed);
               }
           });
    return !failed.load(std::memory_order_relaxed);
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
