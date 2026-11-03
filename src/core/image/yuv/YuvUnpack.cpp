#include "core/image/yuv/YuvUnpack.h"

#include <algorithm>

namespace mviewer::core::yuv
{
namespace
{

// Reads sample 'index' (in samples) of a row and converts it to a code value.
struct SampleReader
{
    int bytes = 1;
    bool bigEndian = false;
    int shift = 0;     // MSB-aligned formats shift down by 16 - bitDepth
    uint16_t mask = 0; // keeps only bitDepth bits

    uint16_t operator()(const uint8_t *row, size_t index) const
    {
        if (bytes == 1)
            return row[index];
        const uint8_t *p = row + index * 2;
        const auto raw = static_cast<uint16_t>(bigEndian ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0]);
        return static_cast<uint16_t>((raw >> shift) & mask);
    }
};

SampleReader makeReader(const Format &f)
{
    SampleReader r;
    r.bytes = bytesPerSample(f);
    r.bigEndian = f.endian == Endian::Big;
    r.shift = f.msbAligned ? 16 - f.bitDepth : 0;
    r.mask = static_cast<uint16_t>((1U << f.bitDepth) - 1U);
    return r;
}

size_t area(int w, int h)
{
    return static_cast<size_t>(w) * static_cast<size_t>(h);
}

// Copies 'g' starting every 'step' samples at 'first' into 'dst' (g.width per row).
void readPlane(const uint8_t *data, const PlaneGeometry &g, const SampleReader &rd, size_t step,
               size_t first, std::vector<uint16_t> &dst, int outWidth)
{
    dst.resize(area(outWidth, g.height));
    for (int y = 0; y < g.height; ++y)
    {
        const uint8_t *row = data + g.offset + static_cast<int64_t>(y) * g.rowBytes;
        uint16_t *out = dst.data() + area(outWidth, y);
        for (int x = 0; x < outWidth; ++x)
            out[x] = rd(row, first + static_cast<size_t>(x) * step);
    }
}

void unpackPlanar(const Format &f, const FrameLayout &l, const uint8_t *data,
                  const SampleReader &rd, YuvPlanes &out)
{
    readPlane(data, l.planes[0], rd, 1, 0, out.y, out.width);
    if (f.subsampling == Subsampling::S400)
        return;
    const bool vu = f.order == PlaneOrder::VU;
    readPlane(data, l.planes[1], rd, 1, 0, vu ? out.v : out.u, out.chromaWidth);
    readPlane(data, l.planes[2], rd, 1, 0, vu ? out.u : out.v, out.chromaWidth);
}

void unpackSemiPlanar(const Format &f, const FrameLayout &l, const uint8_t *data,
                      const SampleReader &rd, YuvPlanes &out)
{
    readPlane(data, l.planes[0], rd, 1, 0, out.y, out.width);
    const bool vu = f.order == PlaneOrder::VU;
    readPlane(data, l.planes[1], rd, 2, vu ? 1 : 0, out.u, out.chromaWidth);
    readPlane(data, l.planes[1], rd, 2, vu ? 0 : 1, out.v, out.chromaWidth);
}

// Byte positions (in samples) of Y0, U, Y1, V inside one 4-sample macropixel.
struct PackedOrder
{
    size_t y0;
    size_t u;
    size_t y1;
    size_t v;
};

PackedOrder packedOrder(Packing p)
{
    switch (p)
    {
    case Packing::UYVY:
        return {1, 0, 3, 2};
    case Packing::YVYU:
        return {0, 3, 2, 1};
    case Packing::YUYV:
    case Packing::None:
    default:
        return {0, 1, 2, 3};
    }
}

void unpackPacked(const Format &f, const FrameLayout &l, const uint8_t *data,
                  const SampleReader &rd, YuvPlanes &out)
{
    const PlaneGeometry &g = l.planes[0];
    const PackedOrder o = packedOrder(f.packing);
    out.y.resize(area(out.width, out.height));
    out.u.resize(area(out.chromaWidth, out.chromaHeight));
    out.v.resize(out.u.size());
    for (int y = 0; y < out.height; ++y)
    {
        const uint8_t *row = data + g.offset + static_cast<int64_t>(y) * g.rowBytes;
        uint16_t *ys = out.y.data() + area(out.width, y);
        for (int c = 0; c < out.chromaWidth; ++c)
        {
            const size_t base = static_cast<size_t>(c) * 4;
            const size_t ci = area(out.chromaWidth, y) + static_cast<size_t>(c);
            ys[2 * c] = rd(row, base + o.y0);
            if (2 * c + 1 < out.width)
                ys[2 * c + 1] = rd(row, base + o.y1);
            out.u[ci] = rd(row, base + o.u);
            out.v[ci] = rd(row, base + o.v);
        }
    }
}

bool fail(std::string *why, const char *msg)
{
    if (why != nullptr)
        *why = msg;
    return false;
}

} // namespace

bool unpackYuvFrame(const Format &f, const uint8_t *data, size_t size, YuvPlanes &out,
                    std::string *why)
{
    out = YuvPlanes{};
    const auto layout = computeFrameLayout(f, why);
    if (!layout)
        return false;
    if (data == nullptr || size < static_cast<uint64_t>(layout->frameBytes))
        return fail(why, "frame data too short");
    out.width = f.width;
    out.height = f.height;
    out.bitDepth = f.bitDepth;
    if (f.subsampling != Subsampling::S400)
    {
        out.chromaWidth = chromaWidth(f);
        out.chromaHeight = chromaHeight(f);
    }
    const SampleReader rd = makeReader(f);
    if (f.subsampling == Subsampling::S400 || f.layout == Layout::Planar)
        unpackPlanar(f, *layout, data, rd, out);
    else if (f.layout == Layout::SemiPlanar)
        unpackSemiPlanar(f, *layout, data, rd, out);
    else
        unpackPacked(f, *layout, data, rd, out);
    return true;
}

uint16_t sampleAt(const std::vector<uint16_t> &plane, int planeW, int planeH, int x, int y)
{
    if (planeW <= 0 || planeH <= 0 || plane.size() < area(planeW, planeH))
        return 0;
    const int cx = std::clamp(x, 0, planeW - 1);
    const int cy = std::clamp(y, 0, planeH - 1);
    return plane[area(planeW, cy) + static_cast<size_t>(cx)];
}

} // namespace mviewer::core::yuv
