#include "core/image/ImageFormats.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/Decoder.h"

#include <QCoreApplication>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace mviewer::core
{

namespace
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Normalize "JPG" / ".jpg" / "jpg" -> "jpg".
std::string normalizeSuffix(const std::string &suffix)
{
    std::string s = lower(suffix);
    if (!s.empty() && s.front() == '.')
        s.erase(s.begin());
    return s;
}

std::vector<std::string> buildSuffixSet()
{
    // DecoderRegistry::supportedExtensions() is the frozen union of every
    // registered decoder's extensions (Qt formats + RAW + plugin decoders).
    std::vector<std::string> out;
    for (const auto &e : Decoder::supportedExtensions())
    {
        std::string s = e;
        if (s.size() > 2 && s.rfind("*.", 0) == 0)
            s = s.substr(2);
        s = normalizeSuffix(s);
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(s);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const std::vector<std::string> &suffixSet()
{
    // The Qt image-format plugins are only fully discoverable once
    // QCoreApplication exists. A set computed earlier (e.g. from a static
    // initializer) would miss WebP/GIF; recompute once when the app appears.
    static std::vector<std::string> cached;
    static bool computed = false;
    static bool computedWithoutApp = false;
    const bool appExists = QCoreApplication::instance() != nullptr;
    if (!computed || (computedWithoutApp && appExists))
    {
        cached = buildSuffixSet();
        computed = true;
        computedWithoutApp = !appExists;
    }
    return cached;
}

} // namespace

const std::vector<std::string> &ImageFormats::supportedSuffixes()
{
    return suffixSet();
}

bool ImageFormats::isSupportedSuffix(const std::string &suffix)
{
    const std::string s = normalizeSuffix(suffix);
    if (s.empty())
        return false;
    const auto &all = suffixSet();
    return std::binary_search(all.begin(), all.end(), s);
}

bool ImageFormats::isSupportedPath(const std::string &path)
{
    const std::filesystem::path p = pathFromUtf8(path);
    return isSupportedSuffix(pathToUtf8(p.extension()));
}

std::vector<std::string> ImageFormats::wildcardFilters()
{
    std::vector<std::string> out;
    for (const auto &s : suffixSet())
        out.push_back("*." + s);
    return out;
}

} // namespace mviewer::core
