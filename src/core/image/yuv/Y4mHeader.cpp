#include "core/image/yuv/Y4mHeader.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace mviewer::core::yuv
{
namespace
{

constexpr char kStreamMagic[] = "YUV4MPEG2";
constexpr size_t kStreamMagicLen = 9;
constexpr char kFrameMagic[] = "FRAME";
constexpr size_t kFrameMagicLen = 5;

struct ChromaSpec
{
    Subsampling subsampling = Subsampling::S420;
    int bitDepth = 8;
    ChromaSiting siting = ChromaSiting::Center;
};

struct HeaderFields
{
    int width = 0;
    int height = 0;
    bool sawW = false;
    bool sawH = false;
    bool sawC = false;
    Subsampling subsampling = Subsampling::S420;
    int bitDepth = 8;
    ChromaSiting siting = ChromaSiting::Center;
    std::string chromaTag = "420jpeg";
    int fpsNum = 0;
    int fpsDen = 0;
    int parNum = 0;
    int parDen = 0;
    char interlace = '?';
    Range range = Range::Limited;
    bool colorRangeExplicit = false;
};

std::optional<Y4mInfo> failHeader(std::string *why, const char *reason)
{
    if (why != nullptr)
        *why = reason;
    return std::nullopt;
}

bool parseUnsigned(std::string_view text, int64_t &out)
{
    if (text.empty())
        return false;
    int64_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        int64_t scaled = 0;
        if (!checkedMul(value, 10, scaled))
            return false;
        int64_t next = 0;
        const int digit = static_cast<int>(c - '0');
        if (!checkedAdd(scaled, static_cast<int64_t>(digit), next))
            return false;
        value = next;
    }
    out = value;
    return true;
}

bool parseInt(std::string_view text, int &out)
{
    int64_t value = 0;
    if (!parseUnsigned(text, value))
        return false;
    if (value > static_cast<int64_t>(std::numeric_limits<int>::max()))
        return false;
    out = static_cast<int>(value);
    return true;
}

bool parseDimension(std::string_view text, int &out)
{
    int64_t value = 0;
    if (!parseUnsigned(text, value))
        return false;
    if (value < 1 || value > static_cast<int64_t>(kMaxDimension))
        return false;
    out = static_cast<int>(value);
    return true;
}

bool assignChroma(ChromaSpec &out, Subsampling sub, int depth, ChromaSiting siting)
{
    out.subsampling = sub;
    out.bitDepth = depth;
    out.siting = siting;
    return true;
}

bool depthSuffix(std::string_view tag, std::string_view &base, int &depth)
{
    if (tag.size() <= 3 || tag[tag.size() - 3] != 'p')
    {
        base = tag;
        depth = 8;
        return true;
    }
    const std::string_view suffix = tag.substr(tag.size() - 2);
    if (suffix == "10")
        depth = 10;
    else if (suffix == "12")
        depth = 12;
    else if (suffix == "16")
        depth = 16;
    else
        return false;
    base = tag.substr(0, tag.size() - 3);
    return true;
}

bool parseChromaTag(std::string_view tag, ChromaSpec &out)
{
    if (tag == "420jpeg" || tag == "420")
        return assignChroma(out, Subsampling::S420, 8, ChromaSiting::Center);
    if (tag == "420paldv")
        return assignChroma(out, Subsampling::S420, 8, ChromaSiting::TopLeft);
    if (tag == "420mpeg2")
        return assignChroma(out, Subsampling::S420, 8, ChromaSiting::Left);
    if (tag == "mono")
        return assignChroma(out, Subsampling::S400, 8, ChromaSiting::Left);
    if (tag == "mono10")
        return assignChroma(out, Subsampling::S400, 10, ChromaSiting::Left);
    if (tag == "mono12")
        return assignChroma(out, Subsampling::S400, 12, ChromaSiting::Left);
    if (tag == "mono16")
        return assignChroma(out, Subsampling::S400, 16, ChromaSiting::Left);

    std::string_view base;
    int depth = 8;
    if (!depthSuffix(tag, base, depth))
        return false;
    if (base == "422")
        return assignChroma(out, Subsampling::S422, depth, ChromaSiting::Left);
    if (base == "444")
        return assignChroma(out, Subsampling::S444, depth, ChromaSiting::Left);
    if (base == "420")
    {
        const ChromaSiting siting = depth == 8 ? ChromaSiting::Center : ChromaSiting::Left;
        return assignChroma(out, Subsampling::S420, depth, siting);
    }
    return false;
}

bool applyDimension(std::string_view value, bool &seen, int &slot, const char *duplicate,
                    const char *bad, std::string &error)
{
    if (seen)
    {
        error = duplicate;
        return false;
    }
    seen = true;
    if (!parseDimension(value, slot))
    {
        error = bad;
        return false;
    }
    return true;
}

void applyRatio(std::string_view value, int &num, int &den)
{
    const size_t colon = value.find(':');
    if (colon == std::string_view::npos || value.find(':', colon + 1) != std::string_view::npos)
        return;
    int parsedNum = 0;
    int parsedDen = 0;
    if (!parseInt(value.substr(0, colon), parsedNum) ||
        !parseInt(value.substr(colon + 1), parsedDen))
        return;
    num = parsedNum;
    den = parsedDen;
}

void applyInterlace(std::string_view value, HeaderFields &fields)
{
    if (value.size() == 1 &&
        (value[0] == 'p' || value[0] == 't' || value[0] == 'b' || value[0] == 'm'))
        fields.interlace = value[0];
    else
        fields.interlace = '?';
}

bool equalsIgnoreCase(std::string_view text, std::string_view lower)
{
    if (text.size() != lower.size())
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != lower[i])
            return false;
    }
    return true;
}

void applyColorRange(std::string_view value, HeaderFields &fields)
{
    constexpr std::string_view kPrefix = "COLORRANGE=";
    if (value.size() <= kPrefix.size() || value.substr(0, kPrefix.size()) != kPrefix)
        return;
    const std::string_view color = value.substr(kPrefix.size());
    if (equalsIgnoreCase(color, "full"))
    {
        fields.range = Range::Full;
        fields.colorRangeExplicit = true;
    }
    else if (equalsIgnoreCase(color, "limited"))
    {
        fields.range = Range::Limited;
        fields.colorRangeExplicit = true;
    }
}

bool applyChroma(std::string_view value, HeaderFields &fields, std::string &error)
{
    if (fields.sawC)
    {
        error = "duplicate chroma";
        return false;
    }
    fields.sawC = true;
    if (value == "444alpha")
    {
        error = "alpha not supported";
        return false;
    }
    ChromaSpec spec;
    if (!parseChromaTag(value, spec))
    {
        error = "unknown chroma";
        return false;
    }
    fields.subsampling = spec.subsampling;
    fields.bitDepth = spec.bitDepth;
    fields.siting = spec.siting;
    fields.chromaTag.assign(value.begin(), value.end());
    return true;
}

bool applyToken(std::string_view token, HeaderFields &fields, std::string &error)
{
    if (token.empty())
        return true;
    const char tag = token[0];
    const std::string_view value = token.substr(1);
    switch (tag)
    {
    case 'W':
        return applyDimension(value, fields.sawW, fields.width, "duplicate width", "bad width",
                              error);
    case 'H':
        return applyDimension(value, fields.sawH, fields.height, "duplicate height", "bad height",
                              error);
    case 'C':
        return applyChroma(value, fields, error);
    case 'F':
        applyRatio(value, fields.fpsNum, fields.fpsDen);
        return true;
    case 'A':
        applyRatio(value, fields.parNum, fields.parDen);
        return true;
    case 'I':
        applyInterlace(value, fields);
        return true;
    case 'X':
        applyColorRange(value, fields);
        return true;
    default:
        return true;
    }
}

bool consumeTokens(const uint8_t *data, size_t newline, HeaderFields &fields, std::string &error)
{
    size_t i = kStreamMagicLen;
    while (i < newline)
    {
        while (i < newline && data[i] == static_cast<uint8_t>(' '))
            ++i;
        if (i >= newline)
            break;
        const size_t begin = i;
        while (i < newline && data[i] != static_cast<uint8_t>(' '))
            ++i;
        const std::string_view token(reinterpret_cast<const char *>(data + begin), i - begin);
        if (!applyToken(token, fields, error))
            return false;
    }
    return true;
}

bool supportedDepth(int depth)
{
    return depth == 8 || depth == 10 || depth == 12 || depth == 16;
}

bool baseTransportOk(const Format &f)
{
    return f.layout == Layout::Planar && f.endian == Endian::Little && !f.msbAligned &&
           f.packing == Packing::None && f.order == PlaneOrder::UV && supportedDepth(f.bitDepth);
}

bool chromaTagFor(const Format &f, std::string &tag)
{
    if (!baseTransportOk(f))
        return false;
    const int depth = f.bitDepth;
    switch (f.subsampling)
    {
    case Subsampling::S400:
        tag = depth == 8 ? "mono" : "mono" + std::to_string(depth);
        return true;
    case Subsampling::S422:
        tag = depth == 8 ? "422" : "422p" + std::to_string(depth);
        return true;
    case Subsampling::S444:
        tag = depth == 8 ? "444" : "444p" + std::to_string(depth);
        return true;
    case Subsampling::S420:
        if (depth == 8)
        {
            if (f.siting == ChromaSiting::Center)
                tag = "420jpeg";
            else if (f.siting == ChromaSiting::Left)
                tag = "420mpeg2";
            else if (f.siting == ChromaSiting::TopLeft)
                tag = "420paldv";
            else
                return false;
            return true;
        }
        // 420p10/p12/p16 are defined as left siting. Other sitings have no tag.
        if (f.siting != ChromaSiting::Left)
            return false;
        tag = "420p" + std::to_string(depth);
        return true;
    }
    return false;
}

} // namespace

std::optional<Y4mInfo> parseY4mHeader(const uint8_t *data, size_t size, std::string *why)
{
    if (data == nullptr || size < kStreamMagicLen)
        return failHeader(why, "truncated header");
    for (size_t i = 0; i < kStreamMagicLen; ++i)
    {
        if (data[i] != static_cast<uint8_t>(kStreamMagic[i]))
            return failHeader(why, "bad magic");
    }
    if (size < kStreamMagicLen + 1 || (data[kStreamMagicLen] != static_cast<uint8_t>(' ') &&
                                       data[kStreamMagicLen] != static_cast<uint8_t>('\n')))
        return failHeader(why, "bad magic");

    const size_t limit = size < kMaxY4mHeaderBytes ? size : kMaxY4mHeaderBytes;
    size_t newline = limit;
    for (size_t i = 0; i < limit; ++i)
    {
        if (data[i] == 0)
            return failHeader(why, "embedded NUL");
        if (data[i] == static_cast<uint8_t>('\n'))
        {
            newline = i;
            break;
        }
    }
    if (newline >= limit)
        return failHeader(why, "missing newline");

    HeaderFields fields;
    std::string error;
    if (!consumeTokens(data, newline, fields, error))
        return failHeader(why, error.c_str());
    if (!fields.sawW)
        return failHeader(why, "missing width");
    if (!fields.sawH)
        return failHeader(why, "missing height");

    Y4mInfo info;
    info.format.width = fields.width;
    info.format.height = fields.height;
    info.format.layout = Layout::Planar;
    info.format.subsampling = fields.subsampling;
    info.format.order = PlaneOrder::UV;
    info.format.packing = Packing::None;
    info.format.bitDepth = fields.bitDepth;
    info.format.msbAligned = false;
    info.format.endian = Endian::Little;
    info.format.matrix = Matrix::BT709;
    info.format.range = fields.range;
    info.format.siting = fields.siting;
    if (!isValid(info.format, why))
    {
        if (why != nullptr && why->empty())
            *why = "invalid format";
        return std::nullopt;
    }

    info.headerBytes = static_cast<int64_t>(newline) + 1;
    info.fpsNum = fields.fpsNum;
    info.fpsDen = fields.fpsDen;
    info.parNum = fields.parNum;
    info.parDen = fields.parDen;
    info.interlace = fields.interlace;
    info.chromaTag = fields.chromaTag;
    info.colorRangeExplicit = fields.colorRangeExplicit;
    return info;
}

int64_t parseY4mFrameHeader(const uint8_t *data, size_t size)
{
    if (data == nullptr || size < kFrameMagicLen)
        return -1;
    for (size_t i = 0; i < kFrameMagicLen; ++i)
    {
        if (data[i] != static_cast<uint8_t>(kFrameMagic[i]))
            return -1;
    }
    const size_t limit = size < kMaxY4mHeaderBytes ? size : kMaxY4mHeaderBytes;
    if (kFrameMagicLen >= limit)
        return -1;
    const uint8_t next = data[kFrameMagicLen];
    if (next != static_cast<uint8_t>(' ') && next != static_cast<uint8_t>('\n'))
        return -1;
    for (size_t i = kFrameMagicLen; i < limit; ++i)
    {
        if (data[i] == static_cast<uint8_t>('\n'))
            return static_cast<int64_t>(i) + 1;
    }
    return -1;
}

std::vector<int64_t> indexY4mFrames(const Y4mInfo &info, int64_t fileSize, const ReadAtFn &read,
                                    size_t maxFrames)
{
    std::vector<int64_t> offsets;
    if (!read || maxFrames == 0 || fileSize <= 0 || info.headerBytes < 0)
        return offsets;
    const std::optional<FrameLayout> layout = computeFrameLayout(info.format);
    if (!layout || layout->frameBytes <= 0)
        return offsets;
    const int64_t frameBytes = layout->frameBytes;

    int64_t cursor = info.headerBytes;
    std::array<uint8_t, kMaxY4mHeaderBytes> buffer{};
    while (offsets.size() < maxFrames && cursor >= 0 && cursor < fileSize)
    {
        const int64_t remain = fileSize - cursor;
        size_t toRead = kMaxY4mHeaderBytes;
        if (remain < static_cast<int64_t>(kMaxY4mHeaderBytes))
            toRead = static_cast<size_t>(remain);
        // Frame headers are usually just "FRAME\n": try a short read first and
        // only fall back to the full window when that does not contain the header.
        constexpr size_t kShortRead = 64;
        int64_t headerLen = -1;
        if (toRead > kShortRead)
        {
            const size_t gotShort = read(cursor, buffer.data(), kShortRead);
            headerLen = parseY4mFrameHeader(buffer.data(), std::min(gotShort, kShortRead));
        }
        if (headerLen < 0)
        {
            const size_t gotRaw = read(cursor, buffer.data(), toRead);
            const size_t got = gotRaw < toRead ? gotRaw : toRead;
            headerLen = parseY4mFrameHeader(buffer.data(), got);
        }
        if (headerLen < 0)
            break;
        int64_t pixel = 0;
        if (!checkedAdd(cursor, headerLen, pixel))
            break;
        int64_t frameEnd = 0;
        if (!checkedAdd(pixel, frameBytes, frameEnd))
            break;
        if (frameEnd > fileSize)
            break;
        offsets.push_back(pixel);
        cursor = frameEnd;
    }
    return offsets;
}

std::string makeY4mHeader(const Format &f, int fpsNum, int fpsDen)
{
    if (fpsNum <= 0 || fpsDen <= 0 || !isValid(f))
        return {};
    std::string tag;
    if (!chromaTagFor(f, tag))
        return {};
    std::string header = "YUV4MPEG2 W";
    header += std::to_string(f.width);
    header += " H";
    header += std::to_string(f.height);
    header += " F";
    header += std::to_string(fpsNum);
    header += ':';
    header += std::to_string(fpsDen);
    header += " Ip A1:1 C";
    header += tag;
    if (f.range == Range::Full)
        header += " XCOLORRANGE=FULL";
    header += '\n';
    return header;
}

} // namespace mviewer::core::yuv
