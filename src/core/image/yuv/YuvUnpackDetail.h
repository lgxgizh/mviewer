#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <cstddef>
#include <cstdint>

// Raw sample access shared by YuvUnpack.cpp and the scaled (thumbnail) renderer,
// which samples straight from the frame bytes without unpacking whole planes.

namespace mviewer::core::yuv::unpack_detail
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

inline SampleReader makeReader(const Format &f)
{
    SampleReader r;
    r.bytes = bytesPerSample(f);
    r.bigEndian = f.endian == Endian::Big;
    r.shift = f.msbAligned ? 16 - f.bitDepth : 0;
    r.mask = static_cast<uint16_t>((1U << f.bitDepth) - 1U);
    return r;
}

// Byte positions (in samples) of Y0, U, Y1, V inside one 4-sample macropixel.
// Y1 is always Y0 + 2, so luma is every second sample from Y0.
struct PackedOrder
{
    size_t y0;
    size_t u;
    size_t y1;
    size_t v;
};

inline PackedOrder packedOrder(Packing p)
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

// One plane as stored in the frame: sample (x, y) is
// reader(data + offset + y * rowBytes, first + x * step).
struct RawPlane
{
    int64_t offset = 0;
    int64_t rowBytes = 0;
    size_t first = 0;
    size_t step = 1;
    int width = 0;
    int height = 0;
};

// Where Y, U and V of 'f' live inside one frame laid out as 'l' (U/V unset for S400).
struct RawPlanes
{
    RawPlane y;
    RawPlane u;
    RawPlane v;
};

inline RawPlanes rawPlanes(const Format &f, const FrameLayout &l)
{
    RawPlanes r;
    const PlaneGeometry &g0 = l.planes[0];
    r.y = {g0.offset, g0.rowBytes, 0, 1, f.width, f.height};
    if (f.subsampling == Subsampling::S400)
        return r;
    const int cw = chromaWidth(f);
    const int ch = chromaHeight(f);
    const bool vu = f.order == PlaneOrder::VU;
    if (f.layout == Layout::Planar)
    {
        const PlaneGeometry &gu = l.planes[vu ? 2 : 1];
        const PlaneGeometry &gv = l.planes[vu ? 1 : 2];
        r.u = {gu.offset, gu.rowBytes, 0, 1, cw, ch};
        r.v = {gv.offset, gv.rowBytes, 0, 1, cw, ch};
    }
    else if (f.layout == Layout::SemiPlanar)
    {
        const PlaneGeometry &g1 = l.planes[1];
        r.u = {g1.offset, g1.rowBytes, vu ? size_t{1} : size_t{0}, 2, cw, ch};
        r.v = {g1.offset, g1.rowBytes, vu ? size_t{0} : size_t{1}, 2, cw, ch};
    }
    else
    {
        const PackedOrder o = packedOrder(f.packing);
        r.y = {g0.offset, g0.rowBytes, o.y0, 2, f.width, f.height};
        r.u = {g0.offset, g0.rowBytes, o.u, 4, cw, ch};
        r.v = {g0.offset, g0.rowBytes, o.v, 4, cw, ch};
    }
    return r;
}

} // namespace mviewer::core::yuv::unpack_detail
