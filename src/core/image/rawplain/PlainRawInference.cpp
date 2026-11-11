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

} // namespace

std::optional<std::pair<int, int>> sizeFromFileName(std::string_view fileName)
{
    const std::size_t n = fileName.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!isDigit(fileName[i]))
        {
            continue;
        }
        if (i > 0 && isDigit(fileName[i - 1]))
        {
            continue;
        }
        std::size_t afterW = 0;
        const int w = parseRun(fileName, i, 5, &afterW);
        if (w < 0)
        {
            continue;
        }
        std::size_t afterSep = 0;
        if (!sepAt(fileName, afterW, &afterSep))
        {
            continue;
        }
        std::size_t afterH = 0;
        const int h = parseRun(fileName, afterSep, 5, &afterH);
        if (h < 0)
        {
            continue;
        }
        if (afterH < n && isDigit(fileName[afterH]))
        {
            continue;
        }
        if (w < 16 || h < 16 || w > kMaxDimension || h > kMaxDimension)
        {
            continue;
        }
        return std::pair<int, int>{w, h};
    }
    return std::nullopt;
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

Resolved resolveForFile(const Params &defaults, std::string_view fileName, int64_t fileSize)
{
    Resolved r;
    r.params = defaults;
    const bool takeW = (defaults.width == 0);
    const bool takeH = (defaults.height == 0);
    if (takeW || takeH)
    {
        if (const auto sz = sizeFromFileName(fileName))
        {
            if (takeW)
            {
                r.params.width = sz->first;
            }
            if (takeH)
            {
                r.params.height = sz->second;
            }
            r.sizeFromName = true;
        }
    }

    auto setWhyUnknown = [&]()
    {
        r.why = "image size unknown: add WIDTHxHEIGHT to the file name or set it in RAW Parameters";
    };

    std::string reason;
    if (!isValid(r.params, &reason))
    {
        r.check = SizeCheck::Invalid;
        r.expectedBytes = frameBytes(r.params);
        if (r.params.width <= 0 || r.params.height <= 0)
        {
            setWhyUnknown();
        }
        else
        {
            r.why = reason;
        }
        const auto guesses = guessFromFileSize(r.params, fileSize);
        if (!guesses.empty() && r.params.width > 0 && r.params.height > 0)
        {
            r.params = guesses.front();
            r.guessed = true;
            r.check = checkFileSize(r.params, fileSize);
            r.expectedBytes = frameBytes(r.params);
            r.known = isValid(r.params, nullptr) &&
                      (r.check == SizeCheck::Exact || r.check == SizeCheck::Larger);
            if (r.known)
            {
                r.why.clear();
            }
        }
        return r;
    }

    r.check = checkFileSize(r.params, fileSize);
    r.expectedBytes = frameBytes(r.params);
    if (r.check != SizeCheck::Exact)
    {
        const auto guesses = guessFromFileSize(r.params, fileSize);
        if (!guesses.empty())
        {
            r.params = guesses.front();
            r.guessed = true;
            r.check = checkFileSize(r.params, fileSize);
            r.expectedBytes = frameBytes(r.params);
        }
    }
    r.known =
        isValid(r.params, nullptr) && (r.check == SizeCheck::Exact || r.check == SizeCheck::Larger);
    if (!r.known)
    {
        if (r.check == SizeCheck::TooSmall)
        {
            const int64_t n = r.expectedBytes;
            const int64_t m = fileSize;
            r.why = "file is smaller than one frame (expected " + std::to_string(n) +
                    " bytes, file has " + std::to_string(m) + ")";
        }
        else
        {
            std::string w2;
            isValid(r.params, &w2);
            r.why = w2;
        }
    }
    return r;
}

} // namespace mviewer::core::rawplain
