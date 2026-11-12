#include "core/image/rawplain/PlainRawFormat.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <system_error>

// ADR 033: file-name size inference, file-size checks and packing guesses.

namespace mviewer::core::rawplain
{

namespace
{

int rescaleBlack(int black, int oldBits, int newBits)
{
    if (oldBits == newBits)
    {
        return black;
    }
    if (newBits > oldBits)
    {
        const int sh = newBits - oldBits;
        if (sh >= 31)
        {
            return 0;
        }
        const int64_t shifted = static_cast<int64_t>(black) << sh;
        if (shifted > std::numeric_limits<int>::max())
        {
            return std::numeric_limits<int>::max();
        }
        return static_cast<int>(shifted);
    }
    const int sh = oldBits - newBits;
    if (sh >= 31)
    {
        return 0;
    }
    return black >> sh;
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

int parseRun(std::string_view s, std::size_t i, int maxDigits, std::size_t *end)
{
    if (i >= s.size() || !isDigit(s[i]))
    {
        return -1;
    }
    int v = 0;
    int n = 0;
    while (i < s.size() && isDigit(s[i]) && n < maxDigits)
    {
        v = v * 10 + (static_cast<int>(s[i]) - '0');
        ++i;
        ++n;
    }
    if (i < s.size() && isDigit(s[i]))
    {
        return -1;
    }
    if (n < 2)
    {
        return -1;
    }
    *end = i;
    return v;
}

bool sepAt(std::string_view s, std::size_t i, std::size_t *after)
{
    if (i >= s.size())
    {
        return false;
    }
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 'x' || c == 'X')
    {
        *after = i + 1;
        return true;
    }
    if (i + 1 < s.size() && c == 0xC3 && static_cast<unsigned char>(s[i + 1]) == 0x97)
    {
        *after = i + 2;
        return true;
    }
    return false;
}

Params variantOf(const Params &base, Packing pack, int bits)
{
    Params q = base;
    q.packing = pack;
    q.bitDepth = bits;
    q.rowStride = 0;
    if (pack != base.packing || bits != base.bitDepth)
    {
        q.blackLevel = rescaleBlack(base.blackLevel, base.bitDepth, bits);
        q.whiteLevel = 0;
    }
    return q;
}

bool alreadyHave(const std::vector<Params> &v, const Params &p)
{
    for (const Params &x : v)
    {
        if (x == p)
        {
            return true;
        }
    }
    return false;
}

void consider(std::vector<Params> *out, const Params &cand, int64_t fileSize)
{
    if (alreadyHave(*out, cand))
    {
        return;
    }
    const int64_t fb = frameBytes(cand);
    if (fb >= 0 && fb == fileSize)
    {
        out->push_back(cand);
    }
}

constexpr const char *kUnknownSizeWhy =
    "image size unknown: add WIDTHxHEIGHT to the file name or set it in RAW Parameters";

constexpr std::pair<int, int> kCommonSizes[] = {
    {640, 480},   {1280, 720},  {1280, 800},  {1280, 960},  {1920, 1080},
    {1920, 1200}, {2048, 1536}, {2592, 1944}, {3264, 2448}, {3840, 2160},
    {4000, 3000}, {4032, 3024}, {4096, 2160}, {4096, 3072}, {4608, 3456},
    {5472, 3648}, {6000, 4000}, {8000, 6000}, {8192, 6144},
};

std::optional<std::pair<int, int>> sizeTokenAt(std::string_view fileName, std::size_t i)
{
    if (!isDigit(fileName[i]))
    {
        return std::nullopt;
    }
    if (i > 0 && isDigit(fileName[i - 1]))
    {
        return std::nullopt;
    }
    std::size_t afterW = 0;
    const int w = parseRun(fileName, i, 5, &afterW);
    if (w < 0)
    {
        return std::nullopt;
    }
    std::size_t afterSep = 0;
    if (!sepAt(fileName, afterW, &afterSep))
    {
        return std::nullopt;
    }
    std::size_t afterH = 0;
    const int h = parseRun(fileName, afterSep, 5, &afterH);
    if (h < 0)
    {
        return std::nullopt;
    }
    if (afterH < fileName.size() && isDigit(fileName[afterH]))
    {
        return std::nullopt;
    }
    if (w < 16 || h < 16 || w > kMaxDimension || h > kMaxDimension)
    {
        return std::nullopt;
    }
    return std::pair<int, int>{w, h};
}

void stampCheck(Resolved &r, int64_t fileSize)
{
    r.check = checkFileSize(r.params, fileSize);
    r.expectedBytes = frameBytes(r.params);
    r.extraBytes = 0;
    if (r.check == SizeCheck::Larger && r.expectedBytes >= 0 && fileSize >= r.expectedBytes)
    {
        r.extraBytes = fileSize - r.expectedBytes;
    }
}

void markKnown(Resolved &r)
{
    r.known =
        isValid(r.params, nullptr) && (r.check == SizeCheck::Exact || r.check == SizeCheck::Larger);
    if (r.known)
    {
        r.why.clear();
    }
}

Params withNamedSize(const Params &defaults, const std::pair<int, int> &sz)
{
    Params trial = defaults;
    if (defaults.width == 0)
    {
        trial.width = sz.first;
    }
    if (defaults.height == 0)
    {
        trial.height = sz.second;
    }
    return trial;
}

std::pair<int, int> pickNamedSize(const Params &defaults,
                                  const std::vector<std::pair<int, int>> &sizes, int64_t fileSize)
{
    for (const auto &sz : sizes)
    {
        if (checkFileSize(withNamedSize(defaults, sz), fileSize) == SizeCheck::Exact)
        {
            return sz;
        }
    }
    for (const auto &sz : sizes)
    {
        if (!guessFromFileSize(withNamedSize(defaults, sz), fileSize).empty())
        {
            return sz;
        }
    }
    return sizes.front();
}

void applyNamedSize(Resolved &r, const Params &defaults, std::string_view fileName,
                    int64_t fileSize)
{
    if (defaults.width != 0 && defaults.height != 0)
    {
        return;
    }
    const auto sizes = sizesFromFileName(fileName);
    if (sizes.empty())
    {
        return;
    }
    std::pair<int, int> sz = sizes.front();
    if (sizes.size() > 1)
    {
        sz = pickNamedSize(defaults, sizes, fileSize);
    }
    r.params = withNamedSize(defaults, sz);
    r.sizeFromName = true;
}

bool takeGuess(Resolved &r, int64_t fileSize)
{
    if (r.params.width <= 0 || r.params.height <= 0)
    {
        return false;
    }
    const auto guesses = guessFromFileSize(r.params, fileSize);
    if (guesses.empty())
    {
        return false;
    }
    r.params = guesses.front();
    r.guessed = true;
    stampCheck(r, fileSize);
    return true;
}

void explainRejected(Resolved &r, int64_t fileSize)
{
    if (r.known)
    {
        return;
    }
    if (r.check == SizeCheck::TooSmall)
    {
        r.why = "file is smaller than one frame (expected " + std::to_string(r.expectedBytes) +
                " bytes, file has " + std::to_string(fileSize) + ")";
        return;
    }
    std::string reason;
    isValid(r.params, &reason);
    r.why = reason;
}

void rejectInvalid(Resolved &r, const std::string &reason, int64_t fileSize)
{
    r.check = SizeCheck::Invalid;
    r.expectedBytes = frameBytes(r.params);
    r.extraBytes = 0;
    if (r.params.width <= 0 || r.params.height <= 0)
    {
        r.why = kUnknownSizeWhy;
    }
    else
    {
        r.why = reason;
    }
    if (!takeGuess(r, fileSize))
    {
        return;
    }
    markKnown(r);
}

} // namespace

std::vector<std::pair<int, int>> sizesFromFileName(std::string_view fileName)
{
    std::vector<std::pair<int, int>> sizes;
    for (std::size_t i = 0; i < fileName.size(); ++i)
    {
        if (const auto token = sizeTokenAt(fileName, i))
        {
            sizes.push_back(*token);
        }
    }
    return sizes;
}

std::optional<std::pair<int, int>> sizeFromFileName(std::string_view fileName)
{
    const auto sizes = sizesFromFileName(fileName);
    if (sizes.empty())
    {
        return std::nullopt;
    }
    return sizes.front();
}

SizeCheck checkFileSize(const Params &p, int64_t fileSize)
{
    if (!isValid(p, nullptr))
    {
        return SizeCheck::Invalid;
    }
    const int64_t need = frameBytes(p);
    if (need < 0 || fileSize < 0)
    {
        return SizeCheck::Invalid;
    }
    if (fileSize < need)
    {
        return SizeCheck::TooSmall;
    }
    if (fileSize == need)
    {
        return SizeCheck::Exact;
    }
    return SizeCheck::Larger;
}

std::vector<Params> guessFromFileSize(const Params &base, int64_t fileSize)
{
    std::vector<Params> out;
    Params self = base;
    self.rowStride = 0;
    consider(&out, self, fileSize);

    const int lsbBits = (base.packing == Packing::Bits8) ? 10 : base.bitDepth;
    consider(&out, variantOf(base, Packing::Lsb16, lsbBits), fileSize);
    consider(&out, variantOf(base, Packing::Mipi10, 10), fileSize);
    consider(&out, variantOf(base, Packing::Mipi12, 12), fileSize);
    consider(&out, variantOf(base, Packing::Bits8, 8), fileSize);
    consider(&out, variantOf(base, Packing::Lsb16, 12), fileSize);
    consider(&out, variantOf(base, Packing::Lsb16, 16), fileSize);
    return out;
}

std::optional<Params> guessCommonSize(const Params &base, int64_t fileSize)
{
    for (const auto &sz : kCommonSizes)
    {
        Params trial = base;
        trial.width = sz.first;
        trial.height = sz.second;
        const auto found = guessFromFileSize(trial, fileSize);
        if (!found.empty())
        {
            return found.front();
        }
    }
    return std::nullopt;
}

Resolved resolveForFile(const Params &defaults, std::string_view fileName, int64_t fileSize)
{
    Resolved r;
    r.params = defaults;
    applyNamedSize(r, defaults, fileName, fileSize);
    std::string reason;
    if (!isValid(r.params, &reason))
    {
        rejectInvalid(r, reason, fileSize);
        return r;
    }
    stampCheck(r, fileSize);
    if (r.check != SizeCheck::Exact)
    {
        takeGuess(r, fileSize);
    }
    markKnown(r);
    explainRejected(r, fileSize);
    return r;
}

} // namespace mviewer::core::rawplain
