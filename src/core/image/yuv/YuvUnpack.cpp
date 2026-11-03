#include "core/image/yuv/YuvUnpack.h"

#include "core/image/yuv/YuvUnpackDetail.h"

#include <algorithm>

namespace mviewer::core::yuv
{
namespace
{

using unpack_detail::makeReader;
using unpack_detail::packedOrder;
using unpack_detail::PackedOrder;
using unpack_detail::SampleReader;

size_t area(int w, int h)
{
    return static_cast<size_t>(w) * static_cast<size_t>(h);
}

// Reads n samples at first, first + Step, ... of one row (same values as SampleReader).
// Compile-time byte width, endianness and step keep the loop branch-free.
template <int Bytes, bool BigEndian, size_t Step>
void readRowT(const uint8_t *row, size_t first, int n, const SampleReader &rd, uint16_t *out)
{
    if constexpr (Bytes == 1)
    {
        const uint8_t *p = row + first;
        for (int x = 0; x < n; ++x)
            out[x] = p[static_cast<size_t>(x) * Step];
    }
    else
    {
        const uint8_t *p = row + first * 2;
        const int shift = rd.shift;
        const uint16_t mask = rd.mask;
        for (int x = 0; x < n; ++x)
        {
            const uint8_t *s = p + static_cast<size_t>(x) * Step * 2;
            const auto raw =
                static_cast<uint16_t>(BigEndian ? (s[0] << 8) | s[1] : (s[1] << 8) | s[0]);
            out[x] = static_cast<uint16_t>((raw >> shift) & mask);
        }
    }
}

template <size_t Step>
void readRow(const uint8_t *row, size_t first, int n, const SampleReader &rd, uint16_t *out)
{
    if (rd.bytes == 1)
        readRowT<1, false, Step>(row, first, n, rd, out);
    else if (rd.bigEndian)
        readRowT<2, true, Step>(row, first, n, rd, out);
    else
        readRowT<2, false, Step>(row, first, n, rd, out);
}

// Copies 'g' starting every 'Step' samples at 'first' into 'dst' (outWidth per row).
template <size_t Step>
void readPlane(const uint8_t *data, const PlaneGeometry &g, const SampleReader &rd, size_t first,
               std::vector<uint16_t> &dst, int outWidth)
{
    dst.resize(area(outWidth, g.height));
    for (int y = 0; y < g.height; ++y)
    {
        const uint8_t *row = data + g.offset + static_cast<int64_t>(y) * g.rowBytes;
        readRow<Step>(row, first, outWidth, rd, dst.data() + area(outWidth, y));
    }
}

void unpackPlanar(const Format &f, const FrameLayout &l, const uint8_t *data,
                  const SampleReader &rd, YuvPlanes &out)
{
    readPlane<1>(data, l.planes[0], rd, 0, out.y, out.width);
    if (f.subsampling == Subsampling::S400)
        return;
    const bool vu = f.order == PlaneOrder::VU;
    readPlane<1>(data, l.planes[1], rd, 0, vu ? out.v : out.u, out.chromaWidth);
    readPlane<1>(data, l.planes[2], rd, 0, vu ? out.u : out.v, out.chromaWidth);
}

void unpackSemiPlanar(const Format &f, const FrameLayout &l, const uint8_t *data,
                      const SampleReader &rd, YuvPlanes &out)
{
    readPlane<1>(data, l.planes[0], rd, 0, out.y, out.width);
    const bool vu = f.order == PlaneOrder::VU;
    readPlane<2>(data, l.planes[1], rd, vu ? 1 : 0, out.u, out.chromaWidth);
    readPlane<2>(data, l.planes[1], rd, vu ? 0 : 1, out.v, out.chromaWidth);
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
        readRow<2>(row, o.y0, out.width, rd, out.y.data() + area(out.width, y));
        readRow<4>(row, o.u, out.chromaWidth, rd, out.u.data() + area(out.chromaWidth, y));
        readRow<4>(row, o.v, out.chromaWidth, rd, out.v.data() + area(out.chromaWidth, y));
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
    // Keep the sample buffers' capacity so a reused YuvPlanes does not reallocate.
    std::vector<uint16_t> keepY = std::move(out.y);
    std::vector<uint16_t> keepU = std::move(out.u);
    std::vector<uint16_t> keepV = std::move(out.v);
    out = YuvPlanes{};
    const auto layout = computeFrameLayout(f, why);
    if (!layout)
        return false;
    if (data == nullptr || size < static_cast<uint64_t>(layout->frameBytes))
        return fail(why, "frame data too short");
    out.width = f.width;
    out.height = f.height;
    out.bitDepth = f.bitDepth;
    out.y = std::move(keepY);
    if (f.subsampling != Subsampling::S400)
    {
        out.u = std::move(keepU);
        out.v = std::move(keepV);
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
