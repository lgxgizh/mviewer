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

} // namespace mviewer::core
