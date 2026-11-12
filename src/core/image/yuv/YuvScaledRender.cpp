#include "core/image/yuv/YuvScaledRender.h"

#include "core/image/yuv/YuvUnpackDetail.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace mviewer::core::yuv::scaled_detail
{
namespace
{

int sampleCoord(int outIndex, int outCount, int srcCount)
{
    if (srcCount <= 1 || outCount <= 1)
    {
        return 0;
    }
    if (outIndex <= 0)
    {
        return 0;
    }
    if (outIndex >= outCount)
    {
        return srcCount - 1;
    }
    const int64_t num = static_cast<int64_t>(outIndex) * static_cast<int64_t>(srcCount);
    int64_t coord = num / outCount;
    if (coord >= srcCount)
    {
        coord = srcCount - 1;
    }
    if (coord < 0)
    {
        coord = 0;
    }
    return static_cast<int>(coord);
}

struct ChromaTap
{
    int i0 = 0;
    int i1 = 0;
    int weight1 = 0; // 0..256
};

ChromaTap mapChroma(int luma, int shift, int planeSize, bool center, bool bilinear)
{
    ChromaTap tap;
    if (planeSize <= 0)
    {
        return tap;
    }
    if (shift <= 0)
    {
        int idx = luma;
        if (idx < 0)
        {
            idx = 0;
        }
        if (idx >= planeSize)
        {
            idx = planeSize - 1;
        }
        tap.i0 = idx;
        tap.i1 = idx;
        return tap;
    }
    const int scale = 1 << shift;
    int adjusted = luma;
    if (center)
    {
        adjusted = luma - (scale / 2);
    }
    if (!bilinear)
    {
        int idx = adjusted < 0 ? 0 : (adjusted >> shift);
        if (idx >= planeSize)
        {
            idx = planeSize - 1;
        }
        tap.i0 = idx;
        tap.i1 = idx;
        return tap;
    }
    if (adjusted < 0)
    {
        adjusted = 0;
    }
    int base = adjusted >> shift;
    const int frac = adjusted & (scale - 1);
    if (base >= planeSize)
    {
        base = planeSize - 1;
    }
    int next = base + 1;
    if (next >= planeSize)
    {
        next = planeSize - 1;
    }
    tap.i0 = base;
    tap.i1 = next;
    tap.weight1 = (frac * 256) / scale;
    return tap;
}

int clampCode(int mixed, int bitDepth)
{
    int clamped = mixed;
    if (clamped < 0)
    {
        clamped = 0;
    }
    const int maxCode = (bitDepth >= 16) ? 65535 : ((1 << bitDepth) - 1);
    if (clamped > maxCode)
    {
        clamped = maxCode;
    }
    return clamped;
}

// Planes already unpacked: sample (x, y) of plane 0 (Y), 1 (U) or 2 (V), clamped.
struct PlanesAccess
{
    const YuvPlanes *p;

    int width(int plane) const
    {
        return plane == 0 ? p->width : p->chromaWidth;
    }
    int height(int plane) const
    {
        return plane == 0 ? p->height : p->chromaHeight;
    }
    // Row handle for a clamped row index.
    const uint16_t *row(int plane, int y) const
    {
        const std::vector<uint16_t> &v = plane == 0 ? p->y : (plane == 1 ? p->u : p->v);
        return v.data() + static_cast<size_t>(y) * static_cast<size_t>(width(plane));
    }
    static uint16_t at(int /*plane*/, const uint16_t *row, int x)
    {
        return row[x];
    }
};

// Raw frame bytes: decodes only the requested samples (same values as unpacking).
struct RawAccess
{
    const uint8_t *data;
    unpack_detail::RawPlanes planes;
    unpack_detail::SampleReader rd;

    const unpack_detail::RawPlane &plane(int i) const
    {
        return i == 0 ? planes.y : (i == 1 ? planes.u : planes.v);
    }
    int width(int i) const
    {
        return plane(i).width;
    }
    int height(int i) const
    {
        return plane(i).height;
    }
    const uint8_t *row(int i, int y) const
    {
        const unpack_detail::RawPlane &g = plane(i);
        return data + g.offset + static_cast<int64_t>(y) * g.rowBytes;
    }
    uint16_t at(int i, const uint8_t *row, int x) const
    {
        const unpack_detail::RawPlane &g = plane(i);
        return rd(row, g.first + static_cast<size_t>(x) * g.step);
    }
};

int clampIndex(int v, int size)
{
    return std::clamp(v, 0, size - 1);
}

// One axis of an output pixel. count == 1 is a nearest sample (1:1 or upscale).
// count > 1 is the source samples that cover the pixel (downscale).
struct LumaCover
{
    int first = 0;
    int count = 0;
};

LumaCover lumaCover(int outIndex, int outCount, int srcCount)
{
    LumaCover cover;
    if (srcCount <= 0 || outCount <= 0)
    {
        return cover;
    }
    if (outCount >= srcCount)
    {
        const int at = sampleCoord(outIndex, outCount, srcCount);
        cover.first = clampIndex(at, srcCount);
        cover.count = 1;
        return cover;
    }
    int begin = static_cast<int>((static_cast<int64_t>(outIndex) * srcCount) / outCount);
    int end = static_cast<int>((static_cast<int64_t>(outIndex + 1) * srcCount) / outCount);
    begin = clampIndex(begin, srcCount);
    if (end <= begin)
    {
        end = begin + 1;
    }
    if (end > srcCount)
    {
        end = srcCount;
    }
    cover.first = begin;
    cover.count = std::max(1, end - begin);
    return cover;
}

template <typename Access, typename Row>
int64_t sumLumaSpan(const Access &acc, const Row *row, int x0, int count)
{
    int64_t sum = 0;
    for (int i = 0; i < count; ++i)
    {
        sum += acc.at(0, row, x0 + i);
    }
    return sum;
}

// Box-average (or single sample) of the luma rectangle, then 12-bit normalize.
template <typename Access>
void fillLuma12(const Access &acc, const std::vector<LumaCover> &covers, const LumaCover &rows,
                int bitDepth, int16_t *y12)
{
    const int outW = static_cast<int>(covers.size());
    const int ny = rows.count > 0 ? rows.count : 0;
    std::vector<int64_t> sums(static_cast<size_t>(std::max(outW, 0)), 0);
    for (int y = 0; y < ny; ++y)
    {
        const auto *row = acc.row(0, rows.first + y);
        for (int ox = 0; ox < outW; ++ox)
        {
            const LumaCover &span = covers[static_cast<size_t>(ox)];
            sums[static_cast<size_t>(ox)] += sumLumaSpan(acc, row, span.first, span.count);
        }
    }
    for (int ox = 0; ox < outW; ++ox)
    {
        // int64: a 65536-wide frame scaled to one pixel is 2^32 samples.
        const int64_t n = static_cast<int64_t>(covers[static_cast<size_t>(ox)].count) * int64_t{ny};
        const int64_t sum = sums[static_cast<size_t>(ox)];
        const int averaged = n > 0 ? static_cast<int>((sum + n / 2) / n) : 0;
        y12[ox] = toTwelveBit(static_cast<uint16_t>(std::max(averaged, 0)), bitDepth);
    }
}

// Luma is a box average when an axis shrinks and nearest otherwise. Chroma taps
// follow mapChroma (same integer math for both accessors).
template <typename Access>
ImageData renderScaledT(const Access &acc, const Format &f, int outW, int outH, ChromaUpsample up)
{
    int64_t pixels = 0;
    int64_t bytes64 = 0;
    if (!checkedMul(outW, outH, pixels) || !checkedMul(pixels, 3, bytes64) || bytes64 <= 0)
    {
        return {};
    }
    if (static_cast<uint64_t>(bytes64) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return {};
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(bytes64));
    const YuvCoeffs coeffs = makeYuvCoeffs(f.matrix, f.range, f.bitDepth);
    const RowConverterFn convert = selectRowConverter();
    const bool gray = f.subsampling == Subsampling::S400;
    const bool bilinear = up == ChromaUpsample::Bilinear;
    const bool centerX = f.siting == ChromaSiting::Center;
    const bool centerY = f.siting == ChromaSiting::Center;
    const int shiftX = chromaShiftX(f.subsampling);
    const int shiftY = chromaShiftY(f.subsampling);
    const int lw = acc.width(0);
    const int lh = acc.height(0);
    const int cw = gray ? 0 : acc.width(1);
    const int ch = gray ? 0 : acc.height(1);
    // Column taps are the same for every output row.
    std::vector<LumaCover> lumaX(static_cast<size_t>(outW));
    std::vector<ChromaTap> chromaX(static_cast<size_t>(gray ? 0 : outW));
    for (int ox = 0; ox < outW; ++ox)
    {
        lumaX[static_cast<size_t>(ox)] = lumaCover(ox, outW, lw);
        if (!gray)
        {
            const int sx = sampleCoord(ox, outW, lw);
            ChromaTap t = mapChroma(sx, shiftX, cw, centerX, bilinear);
            t.i0 = clampIndex(t.i0, cw);
            t.i1 = clampIndex(t.i1, cw);
            chromaX[static_cast<size_t>(ox)] = t;
        }
    }
    std::vector<int16_t> y12(static_cast<size_t>(outW));
    std::vector<int16_t> u12(static_cast<size_t>(gray ? 0 : outW));
    std::vector<int16_t> v12(static_cast<size_t>(gray ? 0 : outW));
    const size_t stride = static_cast<size_t>(outW) * 3u;
    for (int oy = 0; oy < outH; ++oy)
    {
        fillLuma12(acc, lumaX, lumaCover(oy, outH, lh), f.bitDepth, y12.data());
        if (!gray)
        {
            const int sy = sampleCoord(oy, outH, lh);
            const ChromaTap ty = mapChroma(sy, shiftY, ch, centerY, bilinear);
            for (int plane = 1; plane <= 2; ++plane)
            {
                const auto *r0 = acc.row(plane, clampIndex(ty.i0, ch));
                const auto *r1 = acc.row(plane, clampIndex(ty.i1, ch));
                int16_t *out = plane == 1 ? u12.data() : v12.data();
                for (int ox = 0; ox < outW; ++ox)
                {
                    const ChromaTap &tx = chromaX[static_cast<size_t>(ox)];
                    const int s00 = acc.at(plane, r0, tx.i0);
                    const int s10 = acc.at(plane, r0, tx.i1);
                    const int s01 = acc.at(plane, r1, tx.i0);
                    const int s11 = acc.at(plane, r1, tx.i1);
                    const int top = s00 + ((s10 - s00) * tx.weight1) / 256;
                    const int bot = s01 + ((s11 - s01) * tx.weight1) / 256;
                    const int mixed = top + ((bot - top) * ty.weight1) / 256;
                    out[ox] = toTwelveBit(static_cast<uint16_t>(clampCode(mixed, f.bitDepth)),
                                          f.bitDepth);
                }
            }
        }
        convert(y12.data(), gray ? nullptr : u12.data(), gray ? nullptr : v12.data(), outW, coeffs,
                rgb.data() + static_cast<size_t>(oy) * stride);
    }
    return wrapRgb(outW, outH, std::move(rgb));
}

} // namespace

int outputEdge(int srcW, int srcH, int maxEdge, bool horizontal)
{
    const int src = horizontal ? srcW : srcH;
    const int other = horizontal ? srcH : srcW;
    if (maxEdge <= 0 || src <= 0 || other <= 0)
    {
        return std::max(src, 0);
    }
    const int longest = std::max(srcW, srcH);
    if (longest <= maxEdge)
    {
        return src;
    }
    if ((horizontal && srcW >= srcH) || (!horizontal && srcH > srcW))
    {
        return maxEdge;
    }
    const int64_t scaled = (static_cast<int64_t>(src) * static_cast<int64_t>(maxEdge)) / other;
    if (scaled < 1)
    {
        return 1;
    }
    if (scaled > static_cast<int64_t>(std::numeric_limits<int>::max()))
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(scaled);
}

ImageData wrapRgb(int width, int height, std::vector<uint8_t> &&rgb)
{
    ImageData image;
    if (width <= 0 || height <= 0 || rgb.empty())
    {
        return image;
    }
    image.width = width;
    image.height = height;
    image.format = PixelFormat::RGB24;
    image.buffer = std::make_shared<std::vector<uint8_t>>(std::move(rgb));
    return image;
}

ImageData renderScaledPlanes(const YuvPlanes &planes, const Format &f, int outW, int outH,
                             ChromaUpsample up)
{
    return renderScaledT(PlanesAccess{&planes}, f, outW, outH, up);
}

ImageData renderScaledRaw(const uint8_t *data, const FrameLayout &layout, const Format &f, int outW,
                          int outH, ChromaUpsample up)
{
    if (data == nullptr)
    {
        return {};
    }
    const RawAccess acc{data, unpack_detail::rawPlanes(f, layout), unpack_detail::makeReader(f)};
    return renderScaledT(acc, f, outW, outH, up);
}

} // namespace mviewer::core::yuv::scaled_detail
