#include "core/image/yuv/YuvParamInference.h"
#include "core/image/yuv/YuvExtensionMap.h"

#include <array>
#include <optional>

namespace mviewer::core::yuv
{
namespace
{

struct NamedSize
{
    const char *name;
    int width;
    int height;
};

constexpr std::array<NamedSize, 20> kNamedSizes = {{
    {"qcif", 176, 144},    {"cif", 352, 288},     {"4cif", 704, 576},    {"sif", 352, 240},
    {"qvga", 320, 240},    {"vga", 640, 480},     {"480p", 720, 480},    {"576p", 720, 576},
    {"720p", 1280, 720},   {"1080p", 1920, 1080}, {"1080i", 1920, 1080}, {"fhd", 1920, 1080},
    {"1440p", 2560, 1440}, {"qhd", 2560, 1440},   {"2160p", 3840, 2160}, {"uhd", 3840, 2160},
    {"4k", 3840, 2160},    {"dci4k", 4096, 2160}, {"4320p", 7680, 4320}, {"8k", 7680, 4320},
}};

struct CandidateSize
{
    int width;
    int height;
};

constexpr std::array<CandidateSize, 15> kSizeCandidates = {{
    {176, 144},
    {352, 288},
    {704, 576},
    {352, 240},
    {320, 240},
    {640, 480},
    {720, 480},
    {720, 576},
    {1280, 720},
    {1920, 1080},
    {1920, 1088},
    {2560, 1440},
    {3840, 2160},
    {4096, 2160},
    {7680, 4320},
}};

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

char lowerChar(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isSeparator(char c)
{
    switch (c)
    {
    case '_':
    case '-':
    case '.':
    case ' ':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case ',':
    case '+':
    case '@':
        return true;
    default:
        return false;
    }
}

// Digit run starting at pos; returns its length (0 when none).
size_t digitRun(std::string_view text, size_t pos)
{
    size_t n = 0;
    while (pos + n < text.size() && isDigit(text[pos + n]))
        ++n;
    return n;
}

int toInt(std::string_view digits)
{
    int v = 0;
    for (char c : digits)
        v = v * 10 + (c - '0');
    return v;
}

bool findExplicitSize(std::string_view text, int &w, int &h)
{
    size_t i = 0;
    while (i < text.size())
    {
        const size_t n = digitRun(text, i);
        if (n == 0)
        {
            ++i;
            continue;
        }
        const size_t sep = i + n;
        if (n >= 2 && n <= 5 && sep < text.size() &&
            (text[sep] == 'x' || text[sep] == 'X' || text[sep] == '*'))
        {
            const size_t m = digitRun(text, sep + 1);
            if (m >= 2 && m <= 5)
            {
                w = toInt(text.substr(i, n));
                h = toInt(text.substr(sep + 1, m));
                return true;
            }
        }
        i = sep;
    }
    return false;
}

bool namedSize(const std::string &token, int &w, int &h)
{
    for (const NamedSize &s : kNamedSizes)
    {
        if (token == s.name)
        {
            w = s.width;
            h = s.height;
            return true;
        }
    }
    return false;
}

bool tokenIn(const std::string &token, std::initializer_list<const char *> names)
{
    for (const char *name : names)
    {
        if (token == name)
            return true;
    }
    return false;
}

std::optional<Matrix> matrixToken(const std::string &t)
{
    if (tokenIn(t, {"bt601", "601", "rec601", "smpte170m", "bt470bg"}))
        return Matrix::BT601;
    if (tokenIn(t, {"bt709", "709", "rec709"}))
        return Matrix::BT709;
    if (tokenIn(t, {"bt2020", "2020", "rec2020", "bt2100"}))
        return Matrix::BT2020;
    return std::nullopt;
}

std::optional<Range> rangeToken(const std::string &t)
{
    if (tokenIn(t, {"full", "fullrange", "pc", "jpeg", "fr"}))
        return Range::Full;
    if (tokenIn(t, {"limited", "tv", "studio", "lr"}))
        return Range::Limited;
    return std::nullopt;
}

int depthToken(const std::string &t)
{
    if (tokenIn(t, {"8bit"}))
        return 8;
    if (tokenIn(t, {"10bit", "10b"}))
        return 10;
    if (tokenIn(t, {"12bit", "12b"}))
        return 12;
    if (tokenIn(t, {"16bit", "16b"}))
        return 16;
    return 0;
}

const char *subsamplingToken(const std::string &t)
{
    if (tokenIn(t, {"420", "422", "444", "400"}))
        return t == "420" ? "420" : (t == "422" ? "422" : (t == "444" ? "444" : "400"));
    return nullptr;
}

// What one name (file stem or folder) says. Unset fields stay empty.
struct NameHints
{
    std::optional<Format> pixFmt; // only layout/subsampling/depth fields are meaningful
    std::optional<Matrix> matrix;
    std::optional<Range> range;
    int width = 0;
    int height = 0;
    bool size = false;
    bool pixFmtDerived = false; // from a depth / subsampling token, not a pix-fmt name
};

std::string plannedName(const char *sub, int depth)
{
    const std::string s = sub != nullptr ? sub : "420";
    std::string name = s == "400" ? "gray" : "yuv" + s + "p";
    if (depth > 8)
        name += std::to_string(depth) + "le";
    return name;
}

NameHints scanName(std::string_view name)
{
    NameHints hints;
    int depth = 0;
    const char *sub = nullptr;
    const std::vector<std::string> tokens = tokenizeName(name);
    for (const std::string &t : tokens)
    {
        Format f;
        if (!hints.pixFmt && applyPixFmtName(t, f))
            hints.pixFmt = f;
        if (depth == 0)
            depth = depthToken(t);
        if (sub == nullptr)
            sub = subsamplingToken(t);
        if (!hints.matrix)
            hints.matrix = matrixToken(t);
        if (!hints.range)
            hints.range = rangeToken(t);
    }
    if (!hints.pixFmt && (depth != 0 || sub != nullptr))
    {
        Format f;
        if (applyPixFmtName(plannedName(sub, depth), f))
        {
            hints.pixFmt = f;
            hints.pixFmtDerived = true;
        }
    }
    hints.size = parseSizeToken(name, hints.width, hints.height);
    return hints;
}

std::string_view extensionOf(std::string_view fileName, std::string_view &stem)
{
    const size_t dot = fileName.rfind('.');
    if (dot == std::string_view::npos || dot == 0)
    {
        stem = fileName;
        return {};
    }
    stem = fileName.substr(0, dot);
    return fileName.substr(dot + 1);
}

// The user-editable extension mapping (YuvExtensionMap); defaults cover
// .nv12 .nv21 .i420 .yv12 .p010 .yuyv .yuy2 .uyvy .nv16 .y.
// .gray and .grey are reserved for ADR 034 gray dumps, not YUV.
std::optional<Format> extensionPixFmt(std::string_view ext)
{
    const std::optional<std::string> name = mappedPixFmt(ext);
    Format f;
    if (name && applyPixFmtName(*name, f))
        return f;
    return std::nullopt;
}

void copyPixFmt(const Format &from, Format &to)
{
    to.layout = from.layout;
    to.subsampling = from.subsampling;
    to.order = from.order;
    to.packing = from.packing;
    to.bitDepth = from.bitDepth;
    to.msbAligned = from.msbAligned;
    to.endian = from.endian;
}

void applyHints(const NameHints &h, Inference &inf, SizeSource source)
{
    if (!inf.pixFmtKnown && h.pixFmt)
    {
        copyPixFmt(*h.pixFmt, inf.format);
        inf.pixFmtKnown = true;
    }
    if (!inf.matrixKnown && h.matrix)
    {
        inf.format.matrix = *h.matrix;
        inf.matrixKnown = true;
    }
    if (!inf.rangeKnown && h.range)
    {
        inf.format.range = *h.range;
        inf.rangeKnown = true;
    }
    if (!inf.sizeKnown && h.size)
    {
        inf.format.width = h.width;
        inf.format.height = h.height;
        inf.sizeKnown = true;
        inf.sizeSource = source;
    }
}

void sizeFromFileSize(Inference &inf, int64_t fileSize)
{
    int matches = 0;
    CandidateSize found{0, 0};
    for (const CandidateSize &c : kSizeCandidates)
    {
        Format f = inf.format;
        f.width = c.width;
        f.height = c.height;
        const auto layout = computeFrameLayout(f);
        if (layout && layout->frameBytes > 0 && fileSize % layout->frameBytes == 0)
        {
            ++matches;
            found = c;
            inf.sizeCandidates.emplace_back(c.width, c.height);
        }
    }
    if (matches == 1)
    {
        inf.format.width = found.width;
        inf.format.height = found.height;
        inf.sizeKnown = true;
        inf.sizeSource = SizeSource::FileSize;
    }
}

} // namespace

std::vector<std::string> tokenizeName(std::string_view name)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : name)
    {
        if (isSeparator(c))
        {
            if (!current.empty())
                tokens.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(lowerChar(c));
    }
    if (!current.empty())
        tokens.push_back(std::move(current));
    return tokens;
}

bool parseSizeToken(std::string_view text, int &w, int &h)
{
    if (findExplicitSize(text, w, h))
        return true;
    for (const std::string &t : tokenizeName(text))
    {
        if (namedSize(t, w, h))
            return true;
    }
    return false;
}

Inference inferYuvParams(std::string_view fileName, std::string_view folderName, int64_t fileSize)
{
    Inference inf;
    std::string_view stem;
    const std::string_view ext = extensionOf(fileName, stem);
    NameHints stemHints = scanName(stem);
    const auto extFmt = extensionPixFmt(ext);
    if (extFmt && (!stemHints.pixFmt || stemHints.pixFmtDerived))
    {
        stemHints.pixFmt = extFmt;
        stemHints.pixFmtDerived = false;
    }
    applyHints(stemHints, inf, SizeSource::FileName);
    if (!folderName.empty())
        applyHints(scanName(folderName), inf, SizeSource::FolderName);
    if (!inf.sizeKnown && fileSize > 0)
        sizeFromFileSize(inf, fileSize);
    const auto layout = inf.sizeKnown ? computeFrameLayout(inf.format) : std::nullopt;
    if (!layout)
    {
        inf.sizeKnown = false;
        inf.sizeSource = SizeSource::None;
        inf.format.width = 0;
        inf.format.height = 0;
        return inf;
    }
    if (fileSize > 0)
    {
        const FrameCount count = computeFrameCount(fileSize, 0, 0, layout->frameBytes);
        inf.frameCount = count.frames;
        inf.trailingBytes = count.trailingBytes;
    }
    return inf;
}

} // namespace mviewer::core::yuv
