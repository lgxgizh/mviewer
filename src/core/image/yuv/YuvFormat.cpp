#include "core/image/yuv/YuvFormat.h"

#include <limits>

namespace mviewer::core::yuv
{
namespace
{

using L = Layout;
using S = Subsampling;
using O = PlaneOrder;
using P = Packing;
using E = Endian;

struct PixSpec
{
    const char *name;
    Layout layout;
    Subsampling sub;
    PlaneOrder order;
    Packing packing;
    int depth;
    bool msb;
    Endian endian;
};

struct Alias
{
    const char *from;
    const char *to;
};

// One row per canonical pixel-format name. applyPixFmtName and pixFmtName share it,
// so every named format round-trips. 8-bit rows use little endian; there is no
// distinct big-endian name for a single-byte sample.
constexpr PixSpec kSpecs[] = {
    {"yuv420p", L::Planar, S::S420, O::UV, P::None, 8, false, E::Little},
    {"yuv422p", L::Planar, S::S422, O::UV, P::None, 8, false, E::Little},
    {"yuv444p", L::Planar, S::S444, O::UV, P::None, 8, false, E::Little},
    {"yv12", L::Planar, S::S420, O::VU, P::None, 8, false, E::Little},
    {"yuv420p9le", L::Planar, S::S420, O::UV, P::None, 9, false, E::Little},
    {"yuv420p9be", L::Planar, S::S420, O::UV, P::None, 9, false, E::Big},
    {"yuv420p10le", L::Planar, S::S420, O::UV, P::None, 10, false, E::Little},
    {"yuv420p10be", L::Planar, S::S420, O::UV, P::None, 10, false, E::Big},
    {"yuv420p12le", L::Planar, S::S420, O::UV, P::None, 12, false, E::Little},
    {"yuv420p12be", L::Planar, S::S420, O::UV, P::None, 12, false, E::Big},
    {"yuv420p14le", L::Planar, S::S420, O::UV, P::None, 14, false, E::Little},
    {"yuv420p14be", L::Planar, S::S420, O::UV, P::None, 14, false, E::Big},
    {"yuv420p16le", L::Planar, S::S420, O::UV, P::None, 16, false, E::Little},
    {"yuv420p16be", L::Planar, S::S420, O::UV, P::None, 16, false, E::Big},
    {"yuv422p9le", L::Planar, S::S422, O::UV, P::None, 9, false, E::Little},
    {"yuv422p9be", L::Planar, S::S422, O::UV, P::None, 9, false, E::Big},
    {"yuv422p10le", L::Planar, S::S422, O::UV, P::None, 10, false, E::Little},
    {"yuv422p10be", L::Planar, S::S422, O::UV, P::None, 10, false, E::Big},
    {"yuv422p12le", L::Planar, S::S422, O::UV, P::None, 12, false, E::Little},
    {"yuv422p12be", L::Planar, S::S422, O::UV, P::None, 12, false, E::Big},
    {"yuv422p14le", L::Planar, S::S422, O::UV, P::None, 14, false, E::Little},
    {"yuv422p14be", L::Planar, S::S422, O::UV, P::None, 14, false, E::Big},
    {"yuv422p16le", L::Planar, S::S422, O::UV, P::None, 16, false, E::Little},
    {"yuv422p16be", L::Planar, S::S422, O::UV, P::None, 16, false, E::Big},
    {"yuv444p9le", L::Planar, S::S444, O::UV, P::None, 9, false, E::Little},
    {"yuv444p9be", L::Planar, S::S444, O::UV, P::None, 9, false, E::Big},
    {"yuv444p10le", L::Planar, S::S444, O::UV, P::None, 10, false, E::Little},
    {"yuv444p10be", L::Planar, S::S444, O::UV, P::None, 10, false, E::Big},
    {"yuv444p12le", L::Planar, S::S444, O::UV, P::None, 12, false, E::Little},
    {"yuv444p12be", L::Planar, S::S444, O::UV, P::None, 12, false, E::Big},
    {"yuv444p14le", L::Planar, S::S444, O::UV, P::None, 14, false, E::Little},
    {"yuv444p14be", L::Planar, S::S444, O::UV, P::None, 14, false, E::Big},
    {"yuv444p16le", L::Planar, S::S444, O::UV, P::None, 16, false, E::Little},
    {"yuv444p16be", L::Planar, S::S444, O::UV, P::None, 16, false, E::Big},
    {"gray", L::Planar, S::S400, O::UV, P::None, 8, false, E::Little},
    {"gray9le", L::Planar, S::S400, O::UV, P::None, 9, false, E::Little},
    {"gray9be", L::Planar, S::S400, O::UV, P::None, 9, false, E::Big},
    {"gray10le", L::Planar, S::S400, O::UV, P::None, 10, false, E::Little},
    {"gray10be", L::Planar, S::S400, O::UV, P::None, 10, false, E::Big},
    {"gray12le", L::Planar, S::S400, O::UV, P::None, 12, false, E::Little},
    {"gray12be", L::Planar, S::S400, O::UV, P::None, 12, false, E::Big},
    {"gray14le", L::Planar, S::S400, O::UV, P::None, 14, false, E::Little},
    {"gray14be", L::Planar, S::S400, O::UV, P::None, 14, false, E::Big},
    {"gray16le", L::Planar, S::S400, O::UV, P::None, 16, false, E::Little},
    {"gray16be", L::Planar, S::S400, O::UV, P::None, 16, false, E::Big},
    {"nv12", L::SemiPlanar, S::S420, O::UV, P::None, 8, false, E::Little},
    {"nv21", L::SemiPlanar, S::S420, O::VU, P::None, 8, false, E::Little},
    {"nv16", L::SemiPlanar, S::S422, O::UV, P::None, 8, false, E::Little},
    {"nv24", L::SemiPlanar, S::S444, O::UV, P::None, 8, false, E::Little},
    {"p010le", L::SemiPlanar, S::S420, O::UV, P::None, 10, true, E::Little},
    {"p010be", L::SemiPlanar, S::S420, O::UV, P::None, 10, true, E::Big},
    {"p016le", L::SemiPlanar, S::S420, O::UV, P::None, 16, true, E::Little},
    {"p016be", L::SemiPlanar, S::S420, O::UV, P::None, 16, true, E::Big},
    {"p210le", L::SemiPlanar, S::S422, O::UV, P::None, 10, true, E::Little},
    {"p410le", L::SemiPlanar, S::S444, O::UV, P::None, 10, true, E::Little},
    {"yuyv422", L::Packed, S::S422, O::UV, P::YUYV, 8, false, E::Little},
    {"uyvy422", L::Packed, S::S422, O::UV, P::UYVY, 8, false, E::Little},
    {"yvyu422", L::Packed, S::S422, O::VU, P::YVYU, 8, false, E::Little},
};

constexpr Alias kAliases[] = {
    {"i420", "yuv420p"},    {"iyuv", "yuv420p"},    {"yuy2", "yuyv422"}, {"yuyv", "yuyv422"},
    {"uyvy", "uyvy422"},    {"y422", "uyvy422"},    {"yvyu", "yvyu422"}, {"y800", "gray"},
    {"gray8", "gray"},      {"y8", "gray"},         {"i422", "yuv422p"}, {"i444", "yuv444p"},
    {"gray16", "gray16le"}, {"gray10", "gray10le"}, {"p010", "p010le"},  {"p016", "p016le"},
};

constexpr int kSpecCount = static_cast<int>(sizeof(kSpecs) / sizeof(kSpecs[0]));
static_assert(kSpecCount == 58, "canonical pixel-format table size");

std::string lowerAscii(std::string_view text)
{
    std::string out;
    out.resize(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        out[i] = static_cast<char>(c);
    }
    return out;
}

bool isBareNamedDepth(const std::string &rest)
{
    return rest == "9" || rest == "10" || rest == "12" || rest == "14" || rest == "16";
}

// yuv420p10 -> yuv420p10le. A suffix that already carries endian is left alone.
std::string appendDefaultLittle(const std::string &name)
{
    const char *prefixes[] = {"yuv420p", "yuv422p", "yuv444p"};
    for (const char *prefix : prefixes)
    {
        const std::size_t n = std::char_traits<char>::length(prefix);
        if (name.size() <= n || name.compare(0, n, prefix) != 0)
        {
            continue;
        }
        const std::string rest = name.substr(n);
        if (isBareNamedDepth(rest))
        {
            return name + "le";
        }
        return name;
    }
    return name;
}

std::string canonicalizePixToken(const std::string &lowered)
{
    for (const Alias &alias : kAliases)
    {
        if (lowered == alias.from)
        {
            return alias.to;
        }
    }
    return appendDefaultLittle(lowered);
}

const PixSpec *findByName(const std::string &name)
{
    for (const PixSpec &spec : kSpecs)
    {
        if (name == spec.name)
        {
            return &spec;
        }
    }
    return nullptr;
}

const PixSpec *findByFormat(const Format &f)
{
    for (const PixSpec &spec : kSpecs)
    {
        if (spec.layout == f.layout && spec.sub == f.subsampling && spec.order == f.order &&
            spec.packing == f.packing && spec.depth == f.bitDepth && spec.msb == f.msbAligned &&
            spec.endian == f.endian)
        {
            return &spec;
        }
    }
    return nullptr;
}

// Ceil division by 2^shift. Negative numerators use trunc-toward-zero, which is
// ceil for a positive power-of-two divisor, and the shift stays in a safe range.
int ceilDivByShift(int value, int shift)
{
    if (shift <= 0)
    {
        return value;
    }
    const int safeShift = shift > 30 ? 30 : shift;
    const int64_t denom = int64_t(1) << safeShift;
    const int64_t numerator = value;
    const int64_t rounded = numerator >= 0 ? (numerator + denom - 1) / denom : numerator / denom;
    if (rounded > std::numeric_limits<int>::max())
    {
        return std::numeric_limits<int>::max();
    }
    if (rounded < std::numeric_limits<int>::min())
    {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(rounded);
}

const char *layoutError(const Format &f)
{
    if (f.width <= 0 || f.width > kMaxDimension)
    {
        return "width out of range";
    }
    if (f.height <= 0 || f.height > kMaxDimension)
    {
        return "height out of range";
    }
    if (f.bitDepth < 8 || f.bitDepth > 16)
    {
        return "bit depth out of range";
    }
    if (f.msbAligned && f.bitDepth == 8)
    {
        return "msb alignment requires bit depth above 8";
    }
    if (f.layout != L::Planar && f.layout != L::SemiPlanar && f.layout != L::Packed)
    {
        return "invalid layout";
    }
    if (f.subsampling != S::S444 && f.subsampling != S::S422 && f.subsampling != S::S420 &&
        f.subsampling != S::S400)
    {
        return "invalid subsampling";
    }
    if (f.layout == L::Packed)
    {
        if (f.subsampling != S::S422)
        {
            return "packed layout requires 4:2:2 subsampling";
        }
        if (f.packing == P::None)
        {
            return "packed layout requires a packing mode";
        }
    }
    else if (f.packing != P::None)
    {
        return "packing requires packed layout";
    }
    if (f.layout == L::SemiPlanar && f.subsampling == S::S400)
    {
        return "semi-planar gray is not supported";
    }
    return nullptr;
}

bool makePlane(int width, int height, int64_t sampleStride, int64_t bps, int64_t offset,
               PlaneGeometry &plane)
{
    int64_t rowBytes = 0;
    if (!checkedMul(sampleStride, bps, rowBytes))
    {
        return false;
    }
    int64_t bytes = 0;
    if (!checkedMul(rowBytes, height, bytes))
    {
        return false;
    }
    plane.width = width;
    plane.height = height;
    plane.rowBytes = rowBytes;
    plane.offset = offset;
    plane.bytes = bytes;
    return true;
}

std::optional<FrameLayout> reject(std::string *why, const char *reason)
{
    if (why != nullptr)
    {
        *why = reason;
    }
    return std::nullopt;
}

std::optional<FrameLayout> accept(std::string *why, const FrameLayout &layout)
{
    if (why != nullptr)
    {
        why->clear();
    }
    return layout;
}

bool overLimit(int64_t bytes)
{
    return bytes < 0 || bytes > kMaxFrameBytes;
}

bool parsePositiveDim(std::string_view token, int &out)
{
    if (token.empty() || token[0] == '+' || token[0] == '-')
    {
        return false;
    }
    int64_t value = 0;
    const int64_t limit = std::numeric_limits<int64_t>::max();
    for (char ch : token)
    {
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        const int digit = static_cast<int>(ch - '0');
        if (value > (limit - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    if (value <= 0 || value > kMaxDimension)
    {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parseDimensions(std::string_view dim, int &width, int &height)
{
    std::size_t xPos = std::string_view::npos;
    for (std::size_t i = 0; i < dim.size(); ++i)
    {
        if (dim[i] != 'x' && dim[i] != 'X')
        {
            continue;
        }
        if (xPos != std::string_view::npos)
        {
            return false;
        }
        xPos = i;
    }
    if (xPos == std::string_view::npos)
    {
        return false;
    }
    return parsePositiveDim(dim.substr(0, xPos), width) &&
           parsePositiveDim(dim.substr(xPos + 1), height);
}

template <typename T> struct NamedValue
{
    const char *name;
    T value;
};

template <typename T, std::size_t N>
bool lookupNamed(std::string_view text, const NamedValue<T> (&table)[N], T &out)
{
    if (text.size() > 32)
    {
        return false;
    }
    const std::string lowered = lowerAscii(text);
    for (const NamedValue<T> &entry : table)
    {
        if (lowered == entry.name)
        {
            out = entry.value;
            return true;
        }
    }
    return false;
}

} // namespace

int bytesPerSample(const Format &f)
{
    return f.bitDepth <= 8 ? 1 : 2;
}

int chromaShiftX(Subsampling s)
{
    if (s == S::S422 || s == S::S420)
    {
        return 1;
    }
    return 0;
}

int chromaShiftY(Subsampling s)
{
    return s == S::S420 ? 1 : 0;
}

int chromaWidth(const Format &f)
{
    if (f.subsampling == S::S400)
    {
        return 0;
    }
    return ceilDivByShift(f.width, chromaShiftX(f.subsampling));
}

int chromaHeight(const Format &f)
{
    if (f.subsampling == S::S400)
    {
        return 0;
    }
    return ceilDivByShift(f.height, chromaShiftY(f.subsampling));
}

bool checkedMul(int64_t a, int64_t b, int64_t &out)
{
    if (a < 0 || b < 0)
    {
        return false;
    }
    if (a == 0 || b == 0)
    {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<int64_t>::max() / b)
    {
        return false;
    }
    out = a * b;
    return true;
}

bool checkedAdd(int64_t a, int64_t b, int64_t &out)
{
    const int64_t kMax = std::numeric_limits<int64_t>::max();
    const int64_t kMin = std::numeric_limits<int64_t>::min();
    if (b > 0)
    {
        if (a > kMax - b)
        {
            return false;
        }
    }
    else if (b < 0)
    {
        if (a < kMin - b)
        {
            return false;
        }
    }
    out = a + b;
    return true;
}

std::optional<FrameLayout> computeFrameLayout(const Format &f, std::string *why)
{
    if (const char *reason = layoutError(f))
    {
        return reject(why, reason);
    }

    const int64_t bps = bytesPerSample(f);
    const int64_t width = f.width;
    FrameLayout layout;

    if (f.layout == L::Packed)
    {
        const int64_t pairs = (width + 1) / 2;
        int64_t strideSamples = 0;
        if (!checkedMul(pairs, 4, strideSamples) ||
            !makePlane(f.width, f.height, strideSamples, bps, 0, layout.planes[0]))
        {
            return reject(why, "size overflow");
        }
        if (overLimit(layout.planes[0].bytes))
        {
            return reject(why, "frame exceeds 1 GiB");
        }
        layout.planeCount = 1;
        layout.frameBytes = layout.planes[0].bytes;
        return accept(why, layout);
    }

    if (!makePlane(f.width, f.height, width, bps, 0, layout.planes[0]))
    {
        return reject(why, "size overflow");
    }
    if (f.subsampling == S::S400)
    {
        if (overLimit(layout.planes[0].bytes))
        {
            return reject(why, "frame exceeds 1 GiB");
        }
        layout.planeCount = 1;
        layout.frameBytes = layout.planes[0].bytes;
        return accept(why, layout);
    }

    const int cw = chromaWidth(f);
    const int ch = chromaHeight(f);
    const int64_t cw64 = cw;
    if (f.layout == L::Planar)
    {
        if (!makePlane(cw, ch, cw64, bps, layout.planes[0].bytes, layout.planes[1]))
        {
            return reject(why, "size overflow");
        }
        int64_t secondOffset = 0;
        if (!checkedAdd(layout.planes[0].bytes, layout.planes[1].bytes, secondOffset) ||
            !makePlane(cw, ch, cw64, bps, secondOffset, layout.planes[2]))
        {
            return reject(why, "size overflow");
        }
        int64_t total = 0;
        if (!checkedAdd(secondOffset, layout.planes[2].bytes, total))
        {
            return reject(why, "size overflow");
        }
        if (overLimit(total))
        {
            return reject(why, "frame exceeds 1 GiB");
        }
        layout.planeCount = 3;
        layout.frameBytes = total;
        return accept(why, layout);
    }

    int64_t interleaved = 0;
    if (!checkedMul(cw64, 2, interleaved) ||
        !makePlane(cw, ch, interleaved, bps, layout.planes[0].bytes, layout.planes[1]))
    {
        return reject(why, "size overflow");
    }
    int64_t total = 0;
    if (!checkedAdd(layout.planes[0].bytes, layout.planes[1].bytes, total))
    {
        return reject(why, "size overflow");
    }
    if (overLimit(total))
    {
        return reject(why, "frame exceeds 1 GiB");
    }
    layout.planeCount = 2;
    layout.frameBytes = total;
    return accept(why, layout);
}

bool isValid(const Format &f, std::string *why)
{
    return computeFrameLayout(f, why).has_value();
}

std::string pixFmtName(const Format &f)
{
    const PixSpec *spec = findByFormat(f);
    if (spec == nullptr)
    {
        return {};
    }
    return spec->name;
}

bool applyPixFmtName(std::string_view name, Format &inout)
{
    if (name.empty() || name.size() > 64)
    {
        return false;
    }
    const PixSpec *spec = findByName(canonicalizePixToken(lowerAscii(name)));
    if (spec == nullptr)
    {
        return false;
    }
    inout.layout = spec->layout;
    inout.subsampling = spec->sub;
    inout.order = spec->order;
    inout.packing = spec->packing;
    inout.bitDepth = spec->depth;
    inout.msbAligned = spec->msb;
    inout.endian = spec->endian;
    return true;
}

std::string_view matrixName(Matrix m)
{
    return m == Matrix::BT601 ? "bt601" : (m == Matrix::BT2020 ? "bt2020" : "bt709");
}

std::string_view rangeName(Range r)
{
    return r == Range::Full ? "full" : "limited";
}

std::string_view sitingName(ChromaSiting s)
{
    return s == ChromaSiting::Center ? "center" : (s == ChromaSiting::TopLeft ? "topleft" : "left");
}

bool parseMatrix(std::string_view text, Matrix &out)
{
    static constexpr NamedValue<Matrix> kTable[] = {
        {"bt601", Matrix::BT601},   {"601", Matrix::BT601},   {"smpte170m", Matrix::BT601},
        {"bt470bg", Matrix::BT601}, {"bt709", Matrix::BT709}, {"709", Matrix::BT709},
        {"bt2020", Matrix::BT2020}, {"2020", Matrix::BT2020}, {"bt2020nc", Matrix::BT2020}};
    return lookupNamed(text, kTable, out);
}

bool parseRange(std::string_view text, Range &out)
{
    static constexpr NamedValue<Range> kTable[] = {
        {"limited", Range::Limited}, {"tv", Range::Limited}, {"mpeg", Range::Limited},
        {"full", Range::Full},       {"pc", Range::Full},    {"jpeg", Range::Full}};
    return lookupNamed(text, kTable, out);
}

bool parseSiting(std::string_view text, ChromaSiting &out)
{
    static constexpr NamedValue<ChromaSiting> kTable[] = {{"left", ChromaSiting::Left},
                                                          {"center", ChromaSiting::Center},
                                                          {"topleft", ChromaSiting::TopLeft}};
    return lookupNamed(text, kTable, out);
}

std::string toString(const Format &f)
{
    if (!isValid(f))
    {
        return {};
    }
    const std::string pix = pixFmtName(f);
    const std::string_view matrix = matrixName(f.matrix);
    const std::string_view range = rangeName(f.range);
    const std::string_view siting = sitingName(f.siting);
    if (pix.empty() || matrix.empty() || range.empty() || siting.empty())
    {
        return {};
    }
    std::string text = std::to_string(f.width);
    text += 'x';
    text += std::to_string(f.height);
    text += ':';
    text += pix;
    text += ':';
    text += matrix;
    text += ':';
    text += range;
    text += ':';
    text += siting;
    return text;
}

std::optional<Format> parseFormatString(std::string_view text)
{
    if (text.empty() || text.size() > 256)
    {
        return std::nullopt;
    }
    int colons = 0;
    for (char ch : text)
    {
        if (ch == ':')
        {
            ++colons;
        }
    }
    if (colons < 1 || colons > 4)
    {
        return std::nullopt;
    }

    std::string_view parts[5];
    int count = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i != text.size() && text[i] != ':')
        {
            continue;
        }
        parts[count] = text.substr(begin, i - begin);
        ++count;
        begin = i + 1;
    }

    int width = 0;
    int height = 0;
    if (!parseDimensions(parts[0], width, height))
    {
        return std::nullopt;
    }

    Format format;
    format.width = width;
    format.height = height;
    format.matrix = Matrix::BT709;
    format.range = Range::Limited;
    format.siting = ChromaSiting::Left;
    if (!applyPixFmtName(parts[1], format))
    {
        return std::nullopt;
    }
    if (count >= 3 && !parseMatrix(parts[2], format.matrix))
    {
        return std::nullopt;
    }
    if (count >= 4 && !parseRange(parts[3], format.range))
    {
        return std::nullopt;
    }
    if (count >= 5 && !parseSiting(parts[4], format.siting))
    {
        return std::nullopt;
    }
    if (!isValid(format))
    {
        return std::nullopt;
    }
    return format;
}

FrameCount computeFrameCount(int64_t fileSize, int64_t headerBytes, int64_t perFrameOverhead,
                             int64_t frameBytes)
{
    FrameCount none;
    if (fileSize < 0 || headerBytes < 0 || perFrameOverhead < 0 || frameBytes <= 0)
    {
        return none;
    }
    if (fileSize < headerBytes)
    {
        return none;
    }
    int64_t stride = 0;
    if (!checkedAdd(frameBytes, perFrameOverhead, stride) || stride <= 0)
    {
        return none;
    }
    const int64_t payload = fileSize - headerBytes;
    none.frames = payload / stride;
    none.trailingBytes = payload % stride;
    return none;
}

} // namespace mviewer::core::yuv
