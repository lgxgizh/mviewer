#include "core/image/rawplain/PlainRawUnpack.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace mviewer::core::rawplain
{
namespace
{

bool bufferOk(const Params &p, std::size_t size)
{
    if (!isValid(p, nullptr))
    {
        return false;
    }
    const int64_t minb = minRowBytes(p);
    const int64_t stride = effectiveRowStride(p);
    if (minb < 0 || stride < 0)
    {
        return false;
    }
    int64_t need = minb;
    if (p.height > 1)
    {
        int64_t body = 0;
        // stride * (height - 1) + minRowBytes
        const int64_t rows = static_cast<int64_t>(p.height) - 1;
        if (stride > (std::numeric_limits<int64_t>::max() / rows))
        {
            return false;
        }
        body = stride * rows;
        if (body > std::numeric_limits<int64_t>::max() - minb)
        {
            return false;
        }
        need = body + minb;
    }
    return static_cast<int64_t>(size) >= need;
}

std::uint16_t maskOf(int bitDepth)
{
    if (bitDepth <= 0 || bitDepth >= 16)
    {
        return static_cast<std::uint16_t>(0xFFFFu);
    }
    return static_cast<std::uint16_t>((1u << bitDepth) - 1u);
}

std::uint16_t load16(const std::uint8_t *p, ByteOrder order)
{
    if (order == ByteOrder::Little)
    {
        return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
    }
    return static_cast<std::uint16_t>(p[1] | (static_cast<unsigned>(p[0]) << 8));
}

void unpackBits8(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            dst[x] = row[x];
        }
    }
}

int sampleBits1(const std::uint8_t *row, int x)
{
    const int bit = 7 - (x % 8);
    return (row[x / 8] >> bit) & 1;
}

void unpackBits1(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            dst[x] = static_cast<std::uint16_t>(sampleBits1(row, x));
        }
    }
}

void unpackLsb16(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    const std::uint16_t m = maskOf(p.bitDepth);
    const ByteOrder order = p.byteOrder;
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            dst[x] = std::min(load16(row + static_cast<std::size_t>(x) * 2u, order), m);
        }
    }
}

void unpackMsb16(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    const int shift = 16 - p.bitDepth;
    const std::uint16_t m = maskOf(p.bitDepth);
    const ByteOrder order = p.byteOrder;
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            const std::uint16_t v = load16(row + static_cast<std::size_t>(x) * 2u, order);
            dst[x] = static_cast<std::uint16_t>((v >> shift) & m);
        }
    }
}

void unpackMipi10(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        int x = 0;
        int g = 0;
        while (x < w)
        {
            const std::uint8_t *b = row + static_cast<std::size_t>(g) * 5u;
            const unsigned lsb = b[4];
            const int n = (w - x >= 4) ? 4 : (w - x);
            for (int k = 0; k < n; ++k)
            {
                const unsigned lo = (lsb >> (2 * k)) & 0x3u;
                dst[x + k] = static_cast<std::uint16_t>((static_cast<unsigned>(b[k]) << 2) | lo);
            }
            x += n;
            ++g;
        }
    }
}

void unpackMipi12(const Params &p, const std::uint8_t *data, std::uint16_t *out)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        std::uint16_t *dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        int x = 0;
        int g = 0;
        while (x < w)
        {
            const std::uint8_t *b = row + static_cast<std::size_t>(g) * 3u;
            dst[x] =
                static_cast<std::uint16_t>((static_cast<unsigned>(b[0]) << 4) | (b[2] & 0x0Fu));
            if (x + 1 < w)
            {
                dst[x + 1] =
                    static_cast<std::uint16_t>((static_cast<unsigned>(b[1]) << 4) | (b[2] >> 4));
            }
            x += 2;
            ++g;
        }
    }
}

int sampleBits8(const std::uint8_t *row, int x)
{
    return row[x];
}

int sampleLsb16(const std::uint8_t *row, int x, ByteOrder order, std::uint16_t m)
{
    const std::uint16_t value = load16(row + static_cast<std::size_t>(x) * 2u, order);
    return static_cast<int>(std::min(value, m));
}

std::uint16_t maxLsb16Value(const Params &p, const std::uint8_t *data)
{
    const int w = p.width;
    const int h = p.height;
    const int64_t stride = effectiveRowStride(p);
    const ByteOrder order = p.byteOrder;
    std::uint16_t peak = 0;
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *row =
            data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < w; ++x)
        {
            const std::uint16_t value = load16(row + static_cast<std::size_t>(x) * 2u, order);
            if (value > peak)
            {
                peak = value;
            }
        }
    }
    return peak;
}

int sampleMsb16(const std::uint8_t *row, int x, ByteOrder order, int shift, std::uint16_t m)
{
    const std::uint16_t v = load16(row + static_cast<std::size_t>(x) * 2u, order);
    return static_cast<int>((v >> shift) & m);
}

int sampleMipi10(const std::uint8_t *row, int x)
{
    const int g = x / 4;
    const int k = x % 4;
    const std::uint8_t *b = row + static_cast<std::size_t>(g) * 5u;
    const unsigned lo = (static_cast<unsigned>(b[4]) >> (2 * k)) & 0x3u;
    return static_cast<int>((static_cast<unsigned>(b[k]) << 2) | lo);
}

int sampleMipi12(const std::uint8_t *row, int x)
{
    const int g = x / 2;
    const int k = x % 2;
    const std::uint8_t *b = row + static_cast<std::size_t>(g) * 3u;
    if (k == 0)
    {
        return static_cast<int>((static_cast<unsigned>(b[0]) << 4) | (b[2] & 0x0Fu));
    }
    return static_cast<int>((static_cast<unsigned>(b[1]) << 4) | (b[2] >> 4));
}

bool rowCoversSample(const Params &p, std::size_t rowBytes, int x)
{
    std::int64_t need = -1;
    switch (p.packing)
    {
    case Packing::Bits8:
        need = static_cast<std::int64_t>(x) + 1;
        break;
    case Packing::Lsb16:
    case Packing::Msb16:
        need = (static_cast<std::int64_t>(x) + 1) * 2;
        break;
    case Packing::Mipi10:
        need = (static_cast<std::int64_t>(x / 4) + 1) * 5;
        break;
    case Packing::Mipi12:
        need = (static_cast<std::int64_t>(x / 2) + 1) * 3;
        break;
    case Packing::Bits1:
        need = static_cast<std::int64_t>(x / 8) + 1;
        break;
    }
    if (need < 1)
    {
        return false;
    }
    return rowBytes >= static_cast<std::size_t>(need);
}

} // namespace

bool unpackPlainRaw(const Params &p, const std::uint8_t *data, std::size_t size,
                    std::vector<std::uint16_t> &out)
{
    if (data == nullptr || !bufferOk(p, size))
    {
        out.clear();
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(p.width) * static_cast<std::size_t>(p.height);
    out.assign(n, 0);
    switch (p.packing)
    {
    case Packing::Bits8:
        unpackBits8(p, data, out.data());
        break;
    case Packing::Lsb16:
        unpackLsb16(p, data, out.data());
        break;
    case Packing::Msb16:
        unpackMsb16(p, data, out.data());
        break;
    case Packing::Mipi10:
        unpackMipi10(p, data, out.data());
        break;
    case Packing::Mipi12:
        unpackMipi12(p, data, out.data());
        break;
    case Packing::Bits1:
        unpackBits1(p, data, out.data());
        break;
    }
    return true;
}

int sampleAt(const Params &p, const std::uint8_t *data, std::size_t size, int x, int y)
{
    if (data == nullptr || !bufferOk(p, size))
    {
        return -1;
    }
    if (x < 0 || y < 0 || x >= p.width || y >= p.height)
    {
        return -1;
    }
    const int64_t stride = effectiveRowStride(p);
    const std::uint8_t *row = data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    switch (p.packing)
    {
    case Packing::Bits8:
        return sampleBits8(row, x);
    case Packing::Lsb16:
        return sampleLsb16(row, x, p.byteOrder, maskOf(p.bitDepth));
    case Packing::Msb16:
        return sampleMsb16(row, x, p.byteOrder, 16 - p.bitDepth, maskOf(p.bitDepth));
    case Packing::Mipi10:
        return sampleMipi10(row, x);
    case Packing::Mipi12:
        return sampleMipi12(row, x);
    case Packing::Bits1:
        return sampleBits1(row, x);
    }
    return -1;
}

int samplePlainRawRow(const Params &p, const std::uint8_t *row, std::size_t rowBytes, int x)
{
    if (row == nullptr || x < 0 || x >= p.width || !packingSupportsBitDepth(p.packing, p.bitDepth))
    {
        return -1;
    }
    if (!rowCoversSample(p, rowBytes, x))
    {
        return -1;
    }
    switch (p.packing)
    {
    case Packing::Bits8:
        return sampleBits8(row, x);
    case Packing::Lsb16:
        return sampleLsb16(row, x, p.byteOrder, maskOf(p.bitDepth));
    case Packing::Msb16:
        return sampleMsb16(row, x, p.byteOrder, 16 - p.bitDepth, maskOf(p.bitDepth));
    case Packing::Mipi10:
        return sampleMipi10(row, x);
    case Packing::Mipi12:
        return sampleMipi12(row, x);
    case Packing::Bits1:
        return sampleBits1(row, x);
    }
    return -1;
}

int lsb16SignificantBits(const Params &p, const std::uint8_t *data, std::size_t size)
{
    try
    {
        if (p.packing != Packing::Lsb16 || data == nullptr || !bufferOk(p, size))
        {
            return p.bitDepth;
        }
        const unsigned peak = maxLsb16Value(p, data);
        return static_cast<int>(std::bit_width(peak));
    }
    catch (...)
    {
        return p.bitDepth;
    }
}

} // namespace mviewer::core::rawplain
