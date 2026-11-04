#include "core/image/yuv/YuvPixFmtGeneric.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace mviewer::core::yuv::detail
{
namespace
{

const char *subDigits(Subsampling s)
{
    switch (s)
    {
    case Subsampling::S420:
        return "420";
    case Subsampling::S422:
        return "422";
    case Subsampling::S444:
        return "444";
    case Subsampling::S400:
        return nullptr;
    }
    return nullptr;
}

const char *nvCode(Subsampling s, PlaneOrder o)
{
    const bool vu = o == PlaneOrder::VU;
    switch (s)
    {
    case Subsampling::S420:
        return vu ? "21" : "12";
    case Subsampling::S422:
        return vu ? "61" : "16";
    case Subsampling::S444:
        return vu ? "42" : "24";
    case Subsampling::S400:
        return nullptr;
    }
    return nullptr;
}

std::string depthSuffix(const Format &f)
{
    if (f.bitDepth <= 8)
        return {};
    return std::to_string(f.bitDepth) + (f.endian == Endian::Big ? "be" : "le");
}

bool consume(std::string_view &text, std::string_view prefix)
{
    if (text.substr(0, prefix.size()) != prefix)
        return false;
    text.remove_prefix(prefix.size());
    return true;
}

// Parses an optional `depth ("le"|"be")`; returns false on a malformed suffix.
bool parseDepth(std::string_view &text, int &depth, Endian &endian)
{
    depth = 8;
    endian = Endian::Little;
    if (text.empty() || text[0] < '0' || text[0] > '9')
        return true;
    std::size_t n = 0;
    int value = 0;
    while (n < text.size() && n < 2 && text[n] >= '0' && text[n] <= '9')
    {
        value = value * 10 + (text[n] - '0');
        ++n;
    }
    if (value < 9 || value > 16)
        return false;
    text.remove_prefix(n);
    if (consume(text, "le"))
        endian = Endian::Little;
    else if (consume(text, "be"))
        endian = Endian::Big;
    else
        return false;
    depth = value;
    return true;
}

bool parseSub(std::string_view &text, Subsampling &sub)
{
    if (consume(text, "420"))
        sub = Subsampling::S420;
    else if (consume(text, "422"))
        sub = Subsampling::S422;
    else if (consume(text, "444"))
        sub = Subsampling::S444;
    else
        return false;
    return true;
}

void assign(Format &out, Layout layout, Subsampling sub, PlaneOrder order, int depth, bool msb,
            Endian endian)
{
    out.layout = layout;
    out.subsampling = sub;
    out.order = order;
    out.packing = Packing::None;
    out.bitDepth = depth;
    out.msbAligned = msb;
    out.endian = endian;
}

bool parsePlanar(std::string_view text, Format &inout)
{
    PlaneOrder order = PlaneOrder::UV;
    Subsampling sub = Subsampling::S420;
    if (consume(text, "gray"))
    {
        sub = Subsampling::S400;
    }
    else
    {
        if (consume(text, "yuv"))
            order = PlaneOrder::UV;
        else if (consume(text, "yvu"))
            order = PlaneOrder::VU;
        else
            return false;
        if (!parseSub(text, sub) || !consume(text, "p"))
            return false;
    }
    int depth = 8;
    Endian endian = Endian::Little;
    if (!parseDepth(text, depth, endian))
        return false;
    const bool msb = consume(text, "msb");
    if (!text.empty() || (msb && depth == 8))
        return false;
    assign(inout, Layout::Planar, sub, order, depth, msb, endian);
    return true;
}

bool parseSemi(std::string_view text, Format &inout)
{
    if (!consume(text, "nv"))
        return false;
    static constexpr struct
    {
        const char *code;
        Subsampling sub;
        PlaneOrder order;
    } kCodes[] = {
        {"12", Subsampling::S420, PlaneOrder::UV}, {"21", Subsampling::S420, PlaneOrder::VU},
        {"16", Subsampling::S422, PlaneOrder::UV}, {"61", Subsampling::S422, PlaneOrder::VU},
        {"24", Subsampling::S444, PlaneOrder::UV}, {"42", Subsampling::S444, PlaneOrder::VU},
    };
    for (const auto &c : kCodes)
    {
        std::string_view rest = text;
        if (!consume(rest, c.code))
            continue;
        int depth = 8;
        Endian endian = Endian::Little;
        if (consume(rest, "p"))
        {
            if (rest.empty() || !parseDepth(rest, depth, endian) || depth == 8)
                return false;
        }
        const bool lsb = consume(rest, "lsb");
        if (!rest.empty() || (lsb && depth == 8))
            return false;
        assign(inout, Layout::SemiPlanar, c.sub, c.order, depth, depth > 8 && !lsb, endian);
        return true;
    }
    return false;
}

} // namespace

std::string genericPixFmtName(const Format &f)
{
    if (f.bitDepth < 8 || f.bitDepth > 16 || f.packing != Packing::None)
        return {};
    if (f.bitDepth == 8 && f.msbAligned)
        return {};
    if (f.layout == Layout::Planar)
    {
        std::string name;
        if (f.subsampling == Subsampling::S400)
        {
            name = "gray";
        }
        else
        {
            const char *sub = subDigits(f.subsampling);
            if (sub == nullptr)
                return {};
            name = f.order == PlaneOrder::VU ? "yvu" : "yuv";
            name += sub;
            name += 'p';
        }
        name += depthSuffix(f);
        if (f.msbAligned)
            name += "msb";
        return name;
    }
    if (f.layout == Layout::SemiPlanar)
    {
        const char *code = nvCode(f.subsampling, f.order);
        if (code == nullptr)
            return {};
        std::string name = std::string("nv") + code;
        if (f.bitDepth > 8)
        {
            name += 'p';
            name += depthSuffix(f);
            if (!f.msbAligned)
                name += "lsb";
        }
        return name;
    }
    return {};
}

bool parseGenericPixFmtName(std::string_view lowered, Format &inout)
{
    Format f = inout;
    if (parseSemi(lowered, f) || parsePlanar(lowered, f))
    {
        inout = f;
        return true;
    }
    return false;
}

} // namespace mviewer::core::yuv::detail
