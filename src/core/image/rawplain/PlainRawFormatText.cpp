#include "core/image/rawplain/PlainRawFormat.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <system_error>

// ADR 033: canonical text form of plain .raw parameters.

namespace mviewer::core::rawplain
{

namespace
{

const char *packToken(Packing p)
{
    switch (p)
    {
    case Packing::Bits8:
        return "bits8";
    case Packing::Lsb16:
        return "lsb16";
    case Packing::Msb16:
        return "msb16";
    case Packing::Mipi10:
        return "mipi10";
    case Packing::Mipi12:
        return "mipi12";
    }
    return "";
}

bool parsePack(std::string_view t, Packing *out)
{
    if (t == "bits8")
    {
        *out = Packing::Bits8;
        return true;
    }
    if (t == "lsb16")
    {
        *out = Packing::Lsb16;
        return true;
    }
    if (t == "msb16")
    {
        *out = Packing::Msb16;
        return true;
    }
    if (t == "mipi10")
    {
        *out = Packing::Mipi10;
        return true;
    }
    if (t == "mipi12")
    {
        *out = Packing::Mipi12;
        return true;
    }
    return false;
}

const char *cfaToken(Cfa c)
{
    switch (c)
    {
    case Cfa::RGGB:
        return "rggb";
    case Cfa::BGGR:
        return "bggr";
    case Cfa::GRBG:
        return "grbg";
    case Cfa::GBRG:
        return "gbrg";
    case Cfa::Mono:
        return "mono";
    }
    return "";
}

bool parseCfa(std::string_view t, Cfa *out)
{
    if (t == "rggb")
    {
        *out = Cfa::RGGB;
        return true;
    }
    if (t == "bggr")
    {
        *out = Cfa::BGGR;
        return true;
    }
    if (t == "grbg")
    {
        *out = Cfa::GRBG;
        return true;
    }
    if (t == "gbrg")
    {
        *out = Cfa::GBRG;
        return true;
    }
    if (t == "mono")
    {
        *out = Cfa::Mono;
        return true;
    }
    return false;
}

const char *wbToken(WhiteBalance w)
{
    switch (w)
    {
    case WhiteBalance::None:
        return "none";
    case WhiteBalance::GrayWorld:
        return "gray";
    case WhiteBalance::WhitePatch:
        return "patch";
    case WhiteBalance::Manual:
        return "manual";
    }
    return "";
}

bool parseWb(std::string_view t, WhiteBalance *out)
{
    if (t == "none")
    {
        *out = WhiteBalance::None;
        return true;
    }
    if (t == "gray")
    {
        *out = WhiteBalance::GrayWorld;
        return true;
    }
    if (t == "patch")
    {
        *out = WhiteBalance::WhitePatch;
        return true;
    }
    if (t == "manual")
    {
        *out = WhiteBalance::Manual;
        return true;
    }
    return false;
}

std::string formatDouble(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0')
    {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.')
    {
        s.pop_back();
    }
    if (s.empty())
    {
        s = "0";
    }
    return s;
}

bool parseInt32(std::string_view t, int *out)
{
    if (t.empty())
    {
        return false;
    }
    int v = 0;
    const char *b = t.data();
    const char *e = b + t.size();
    auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc{} || r.ptr != e)
    {
        return false;
    }
    *out = v;
    return true;
}

bool parseInt64(std::string_view t, int64_t *out)
{
    if (t.empty())
    {
        return false;
    }
    int64_t v = 0;
    const char *b = t.data();
    const char *e = b + t.size();
    auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc{} || r.ptr != e)
    {
        return false;
    }
    *out = v;
    return true;
}

bool parseDouble(std::string_view t, double *out)
{
    if (t.empty() || t.size() > 63)
    {
        return false;
    }
    char buf[64];
    std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    char *end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end == buf || *end != '\0' || !std::isfinite(v))
    {
        return false;
    }
    *out = v;
    return true;
}

bool applyKey(Params *p, std::string_view key, std::string_view val)
{
    if (key == "w")
    {
        return parseInt32(val, &p->width);
    }
    if (key == "h")
    {
        return parseInt32(val, &p->height);
    }
    if (key == "bits")
    {
        return parseInt32(val, &p->bitDepth);
    }
    if (key == "pack")
    {
        return parsePack(val, &p->packing);
    }
    if (key == "order")
    {
        if (val == "le")
        {
            p->byteOrder = ByteOrder::Little;
            return true;
        }
        if (val == "be")
        {
            p->byteOrder = ByteOrder::Big;
            return true;
        }
        return false;
    }
    if (key == "offset")
    {
        return parseInt64(val, &p->headerOffset);
    }
    if (key == "stride")
    {
        return parseInt64(val, &p->rowStride);
    }
    if (key == "black")
    {
        return parseInt32(val, &p->blackLevel);
    }
    if (key == "white")
    {
        return parseInt32(val, &p->whiteLevel);
    }
    if (key == "cfa")
    {
        return parseCfa(val, &p->cfa);
    }
    if (key == "wb")
    {
        return parseWb(val, &p->wb);
    }
    if (key == "wbr")
    {
        return parseDouble(val, &p->wbGainR);
    }
    if (key == "wbb")
    {
        return parseDouble(val, &p->wbGainB);
    }
    if (key == "gain")
    {
        return parseDouble(val, &p->gain);
    }
    if (key == "gamma")
    {
        int g = 0;
        if (!parseInt32(val, &g) || (g != 0 && g != 1))
        {
            return false;
        }
        p->srgbGamma = (g == 1);
        return true;
    }
    return false;
}

int keyIndex(std::string_view key)
{
    static const char *kKeys[] = {"w",      "h",      "bits",  "pack",  "order",
                                  "offset", "stride", "black", "white", "cfa",
                                  "wb",     "wbr",    "wbb",   "gain",  "gamma"};
    for (int i = 0; i < 15; ++i)
    {
        if (key == kKeys[i])
        {
            return i;
        }
    }
    return -1;
}

} // namespace

std::string toString(const Params &p)
{
    std::string s;
    s.reserve(160);
    s += "w=";
    s += std::to_string(p.width);
    s += ";h=";
    s += std::to_string(p.height);
    s += ";bits=";
    s += std::to_string(p.bitDepth);
    s += ";pack=";
    s += packToken(p.packing);
    s += ";order=";
    s += (p.byteOrder == ByteOrder::Little) ? "le" : "be";
    s += ";offset=";
    s += std::to_string(p.headerOffset);
    s += ";stride=";
    s += std::to_string(p.rowStride);
    s += ";black=";
    s += std::to_string(p.blackLevel);
    s += ";white=";
    s += std::to_string(p.whiteLevel);
    s += ";cfa=";
    s += cfaToken(p.cfa);
    s += ";wb=";
    s += wbToken(p.wb);
    s += ";wbr=";
    s += formatDouble(p.wbGainR);
    s += ";wbb=";
    s += formatDouble(p.wbGainB);
    s += ";gain=";
    s += formatDouble(p.gain);
    s += ";gamma=";
    s += p.srgbGamma ? "1" : "0";
    return s;
}

std::optional<Params> parseParams(std::string_view text)
{
    Params p{};
    if (text.empty())
    {
        return p;
    }
    bool seen[15] = {};
    std::size_t i = 0;
    while (i < text.size())
    {
        const std::size_t semi = text.find(';', i);
        const std::size_t end = (semi == std::string_view::npos) ? text.size() : semi;
        const std::string_view tok = text.substr(i, end - i);
        const std::size_t eq = tok.find('=');
        if (eq == std::string_view::npos || eq == 0)
        {
            return std::nullopt;
        }
        const std::string_view key = tok.substr(0, eq);
        const std::string_view val = tok.substr(eq + 1);
        const int ki = keyIndex(key);
        if (ki < 0)
        {
            return std::nullopt;
        }
        if (seen[ki])
        {
            return std::nullopt;
        }
        seen[ki] = true;
        if (!applyKey(&p, key, val))
        {
            return std::nullopt;
        }
        if (semi == std::string_view::npos)
        {
            break;
        }
        i = end + 1;
    }
    return p;
}

} // namespace mviewer::core::rawplain
