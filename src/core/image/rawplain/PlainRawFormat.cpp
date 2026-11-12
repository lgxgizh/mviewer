#include "core/image/rawplain/PlainRawFormat.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <system_error>

// ADR 033 / ADR 034: plain dump size math and validation.

namespace mviewer::core::rawplain
{

namespace
{

bool mulOverflow(int64_t a, int64_t b, int64_t *out)
{
    if (a == 0 || b == 0)
    {
        *out = 0;
        return false;
    }
    if (a > 0 && b > 0)
    {
        if (a > std::numeric_limits<int64_t>::max() / b)
        {
            return true;
        }
    }
    else if (a < 0 && b < 0)
    {
        if (a < std::numeric_limits<int64_t>::max() / b)
        {
            return true;
        }
    }
    else if (a > 0 && b < 0)
    {
        if (b < std::numeric_limits<int64_t>::min() / a)
        {
            return true;
        }
    }
    else
    {
        if (a < std::numeric_limits<int64_t>::min() / b)
        {
            return true;
        }
    }
    *out = a * b;
    return false;
}

bool addOverflow(int64_t a, int64_t b, int64_t *out)
{
    if (b > 0 && a > std::numeric_limits<int64_t>::max() - b)
    {
        return true;
    }
    if (b < 0 && a < std::numeric_limits<int64_t>::min() - b)
    {
        return true;
    }
    *out = a + b;
    return false;
}

bool finiteInRange(double v, double lo, double hi)
{
    return std::isfinite(v) && v >= lo && v <= hi;
}

} // namespace

int64_t minRowBytes(const Params &p)
{
    if (p.width <= 0 || !packingSupportsBitDepth(p.packing, p.bitDepth))
    {
        return -1;
    }
    const int64_t w = p.width;
    int64_t n = 0;
    switch (p.packing)
    {
    case Packing::Bits8:
        return w;
    case Packing::Lsb16:
    case Packing::Msb16:
        if (mulOverflow(w, 2, &n))
        {
            return -1;
        }
        return n;
    case Packing::Mipi10:
    {
        const int64_t groups = (w + 3) / 4;
        if (mulOverflow(groups, 5, &n))
        {
            return -1;
        }
        return n;
    }
    case Packing::Mipi12:
    {
        const int64_t groups = (w + 1) / 2;
        if (mulOverflow(groups, 3, &n))
        {
            return -1;
        }
        return n;
    }
    case Packing::Bits1:
        return (w + 7) / 8;
    }
    return -1;
}

int64_t effectiveRowStride(const Params &p)
{
    if (p.rowStride > 0)
    {
        return p.rowStride;
    }
    return minRowBytes(p);
}

int effectiveWhiteLevel(const Params &p)
{
    if (p.whiteLevel > 0)
    {
        return p.whiteLevel;
    }
    if (p.bitDepth < 1 || p.bitDepth > 30)
    {
        return 0;
    }
    return (1 << p.bitDepth) - 1;
}

int64_t frameBytes(const Params &p)
{
    const int64_t stride = effectiveRowStride(p);
    if (stride < 0 || p.height <= 0 || p.headerOffset < 0)
    {
        return -1;
    }
    int64_t body = 0;
    if (mulOverflow(stride, static_cast<int64_t>(p.height), &body))
    {
        return -1;
    }
    int64_t total = 0;
    if (addOverflow(p.headerOffset, body, &total))
    {
        return -1;
    }
    return total;
}

bool packingSupportsBitDepth(Packing packing, int bitDepth)
{
    switch (packing)
    {
    case Packing::Bits8:
        return bitDepth == 8;
    case Packing::Mipi10:
        return bitDepth == 10;
    case Packing::Mipi12:
        return bitDepth == 12;
    case Packing::Lsb16:
    case Packing::Msb16:
        return bitDepth >= 8 && bitDepth <= 16;
    case Packing::Bits1:
        return bitDepth == 1;
    }
    return false;
}

bool isValidDefaults(const Params &p, std::string *why)
{
    auto fail = [&](const char *msg)
    {
        if (why != nullptr)
        {
            *why = msg;
        }
        return false;
    };
    if (!packingSupportsBitDepth(p.packing, p.bitDepth))
    {
        return fail("packing does not match bitDepth");
    }
    if (p.headerOffset < 0)
    {
        return fail("negative offset");
    }
    const int64_t minb = minRowBytes(p);
    if (p.width > 0 && minb < 0)
    {
        return fail("row size overflow");
    }
    if (p.rowStride != 0)
    {
        if (p.rowStride < 0)
        {
            return fail("stride too small");
        }
        if (p.width > 0 && minb >= 0 && p.rowStride < minb)
        {
            return fail("stride too small");
        }
    }
    const int maxWhite = (p.bitDepth >= 1 && p.bitDepth <= 30) ? ((1 << p.bitDepth) - 1) : 0;
    const int white = effectiveWhiteLevel(p);
    if (p.blackLevel < 0 || !(p.blackLevel < white) || white > maxWhite)
    {
        return fail("black >= white");
    }
    if (!finiteInRange(p.wbGainR, 0.1, 16.0) || !finiteInRange(p.wbGainB, 0.1, 16.0))
    {
        return fail("white-balance gain out of range");
    }
    if (!finiteInRange(p.gain, 0.125, 64.0))
    {
        return fail("gain out of range");
    }
    return true;
}

bool isValid(const Params &p, std::string *why)
{
    auto fail = [&](const char *msg)
    {
        if (why != nullptr)
        {
            *why = msg;
        }
        return false;
    };
    if (p.width < 1 || p.height < 1)
    {
        return fail("zero size");
    }
    if (p.width > kMaxDimension || p.height > kMaxDimension)
    {
        return fail("width or height above 65536");
    }
    int64_t pixels = 0;
    if (mulOverflow(static_cast<int64_t>(p.width), static_cast<int64_t>(p.height), &pixels) ||
        pixels > kMaxPixels)
    {
        return fail("too large");
    }
    return isValidDefaults(p, why);
}

Params grayDefaultParams()
{
    Params p;
    p.width = 0;
    p.height = 0;
    p.bitDepth = 8;
    p.packing = Packing::Bits8;
    p.byteOrder = ByteOrder::Little;
    p.headerOffset = 0;
    p.rowStride = 0;
    p.blackLevel = 0;
    p.whiteLevel = 0;
    p.cfa = Cfa::Mono;
    p.wb = WhiteBalance::None;
    p.wbGainR = 1.0;
    p.wbGainB = 1.0;
    p.gain = 1.0;
    p.srgbGamma = false;
    return p;
}

Params normalizedForKind(const Params &p, DumpKind kind)
{
    if (kind != DumpKind::Gray)
    {
        return p;
    }
    Params q = p;
    q.cfa = Cfa::Mono;
    q.wb = WhiteBalance::None;
    q.wbGainR = 1.0;
    q.wbGainB = 1.0;
    return q;
}

} // namespace mviewer::core::rawplain
