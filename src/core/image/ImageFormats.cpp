#include "core/image/ImageFormats.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/Decoder.h"

#include <QCoreApplication>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

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

// Rebuilt whenever the inputs change: the decoder registry's extension union
// (plugins register/remove decoders at runtime) and whether a QCoreApplication
// exists yet (the Qt image plugins are only fully discoverable once it does).
// Previously the set was computed once — with a one-shot "the app appeared"
// repair — so a decoder registered later stayed invisible to the file list.
std::string suffixFingerprint()
{
    std::string f = QCoreApplication::instance() != nullptr ? "app|" : "noapp|";
    for (const auto &e : Decoder::supportedExtensions())
    {
        f += e;
        f += ',';
    }
    return f;
}

const std::vector<std::string> &suffixSet()
{
    static std::mutex mtx;
    static std::vector<std::string> cached;
    static std::string cachedFingerprint;
    const std::string fingerprint = suffixFingerprint();
    std::lock_guard<std::mutex> lk(mtx);
    if (fingerprint != cachedFingerprint)
    {
        cached = buildSuffixSet();
        cachedFingerprint = fingerprint;
    }
    return cached;
}

} // namespace

std::vector<std::string> ImageFormats::supportedSuffixes()
{
    // By value: handing out a reference to shared mutable state let a caller
    // iterate a vector that another thread could rebuild underneath it.
    return suffixSet();
}

bool ImageFormats::isSupportedSuffix(const std::string &suffix)
{
    const std::string s = normalizeSuffix(suffix);
    if (s.empty())
        return false;
    const std::vector<std::string> &all = suffixSet();
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

const std::vector<std::string_view> &rawExtensions()
{
    // Union of the historical decoder, metadata, thumbnail, and combo lists.
    static const std::vector<std::string_view> kExts = {
        "3fr", "arw", "cr2", "cr3", "crw", "dcr", "dng", "erf", "fff", "iiq", "kdc", "mef", "mos",
        "mrw", "nef", "nrw", "orf", "pef", "raf", "raw", "rw2", "rwz", "sr2", "srw", "x3f"};
    return kExts;
}

bool isRawExtension(std::string_view extNoDot)
{
    const std::string suffix = normalizeSuffix(std::string(extNoDot));
    if (suffix.empty())
        return false;
    for (const std::string_view ext : rawExtensions())
    {
        if (suffix == ext)
            return true;
    }
    return false;
}

bool isRawPath(std::string_view path)
{
    const std::filesystem::path file = pathFromUtf8(std::string(path));
    return isRawExtension(pathToUtf8(file.extension()));
}

std::string rawTypeFilterData()
{
    std::string joined;
    for (const std::string_view ext : rawExtensions())
    {
        if (!joined.empty())
            joined.push_back(',');
        joined.append(ext.data(), ext.size());
    }
    return joined;
}

bool suffixMatchesTypeFilter(std::string_view suffixNoDot, std::string_view typeFilterCsv)
{
    if (typeFilterCsv.empty())
        return true;
    const std::string suffix = normalizeSuffix(std::string(suffixNoDot));
    if (suffix.empty())
        return false;

    const std::string filter(typeFilterCsv);
    std::size_t begin = 0;
    while (begin < filter.size())
    {
        const std::size_t comma = filter.find(',', begin);
        const std::size_t end = comma == std::string::npos ? filter.size() : comma;
        std::string token = filter.substr(begin, end - begin);
        const auto first = token.find_first_not_of(" \t");
        if (first == std::string::npos)
            token.clear();
        else
        {
            const auto last = token.find_last_not_of(" \t");
            token = token.substr(first, last - first + 1);
        }
        token = normalizeSuffix(token);
        if (token == suffix)
            return true;
        if (token == "raw" && isRawExtension(suffix))
            return true;
        if (token == "tiff" && (suffix == "tif" || suffix == "tiff"))
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}

} // namespace mviewer::core
