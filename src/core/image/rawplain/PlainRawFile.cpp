#include "core/image/rawplain/PlainRawFile.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/rawplain/PlainRawRender.h"
#include "core/image/rawplain/PlainRawUnpack.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mviewer::core::rawplain
{
namespace
{

std::string lastComponent(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1);
}

bool equalsRawExt(std::string_view ext)
{
    if (ext.size() != 3)
        return false;
    auto lower = [](char c) -> char
    {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z')
            return static_cast<char>(u - 'A' + 'a');
        return static_cast<char>(u);
    };
    return lower(ext[0]) == 'r' && lower(ext[1]) == 'a' && lower(ext[2]) == 'w';
}

const char *cfaLabel(Cfa cfa)
{
    switch (cfa)
    {
    case Cfa::RGGB:
        return "RGGB";
    case Cfa::BGGR:
        return "BGGR";
    case Cfa::GRBG:
        return "GRBG";
    case Cfa::GBRG:
        return "GBRG";
    case Cfa::Mono:
        return "Mono";
    }
    return "RGGB";
}

void setWhy(std::string *why, const std::string &text)
{
    if (why != nullptr)
        *why = text;
}

bool payloadSize(const Params &p, int64_t fileSize, int64_t &bytes, std::string &why)
{
    const int64_t frame = frameBytes(p);
    if (frame < 0 || p.headerOffset < 0 || frame < p.headerOffset)
    {
        why = "invalid frame size";
        return false;
    }
    bytes = frame - p.headerOffset;
    if (bytes < 0 || fileSize < 0 || fileSize < frame)
    {
        why = "frame is larger than the file";
        return false;
    }
    if (static_cast<std::uint64_t>(bytes) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        why = "frame is too large";
        return false;
    }
    const auto maxAsk = static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
    if (static_cast<std::uint64_t>(bytes) > maxAsk)
    {
        why = "frame is too large";
        return false;
    }
    return true;
}

bool readExact(const std::string &path, int64_t offset, std::vector<std::uint8_t> &out)
{
    if (offset < 0 || offset > static_cast<int64_t>(std::numeric_limits<std::streamoff>::max()))
        return false;
    std::ifstream file(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!file.is_open())
        return false;
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good())
        return false;
    const auto ask = static_cast<std::streamsize>(out.size());
    file.read(reinterpret_cast<char *>(out.data()), ask);
    return file.gcount() == ask;
}

int64_t sizeOfFile(const std::filesystem::path &native, bool &ok)
{
    std::error_code ec;
    const auto sz = std::filesystem::file_size(native, ec);
    if (ec || sz > static_cast<std::uintmax_t>(std::numeric_limits<int64_t>::max()))
    {
        ok = false;
        return -1;
    }
    ok = true;
    return static_cast<int64_t>(sz);
}

PlainRawFileInfo unopenable()
{
    PlainRawFileInfo info;
    info.resolved.known = false;
    info.resolved.why = "cannot open file";
    info.fileSize = -1;
    return info;
}

std::string sampleLine(const Params &p, const std::vector<std::uint16_t> &samples, int x, int y)
{
    if (x < 0 || y < 0 || x >= p.width || y >= p.height)
        return {};
    const int64_t idx =
        static_cast<int64_t>(y) * static_cast<int64_t>(p.width) + static_cast<int64_t>(x);
    if (idx < 0 || static_cast<std::uint64_t>(idx) >= static_cast<std::uint64_t>(samples.size()))
        return {};
    char channel = 'G';
    if (p.cfa == Cfa::Mono)
        channel = 'Y';
    else
    {
        switch (cfaColorAt(p.cfa, x, y))
        {
        case CfaColor::Red:
            channel = 'R';
            break;
        case CfaColor::Green:
            channel = 'G';
            break;
        case CfaColor::Blue:
            channel = 'B';
            break;
        }
    }
    const auto value = samples[static_cast<std::size_t>(idx)];
    std::string text = "RAW ";
    text.push_back(channel);
    text.push_back('=');
    text += std::to_string(static_cast<unsigned>(value));
    text += " (";
    text += std::to_string(p.bitDepth);
    text += "-bit)";
    return text;
}

struct SampleCache
{
    std::string key;
    Params params;
    std::vector<std::uint16_t> samples;
};

std::mutex &sampleMutex()
{
    static std::mutex mutex;
    return mutex;
}

SampleCache &sampleCache()
{
    static SampleCache cache;
    return cache;
}

std::string cacheIdentity(const std::string &path, const Params &p, int64_t fileSize)
{
    std::string key = plainRawParamKey(path);
    key.push_back('|');
    key += toString(p);
    key.push_back('|');
    key += std::to_string(fileSize);
    return key;
}

} // namespace

bool isPlainRawPath(const std::string &path)
{
    try
    {
        const std::string name = lastComponent(path);
        const std::size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= name.size())
            return false;
        return equalsRawExt(std::string_view(name).substr(dot + 1));
    }
    catch (...)
    {
        return false;
    }
}

std::string plainRawParamKey(const std::string &path)
{
    try
    {
        return mviewer::core::normalizeKey(path);
    }
    catch (...)
    {
        return {};
    }
}

PlainRawParamStore &PlainRawParamStore::instance()
{
    static PlainRawParamStore store;
    return store;
}

Params PlainRawParamStore::defaults() const
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_defaults;
    }
    catch (...)
    {
        return {};
    }
}

void PlainRawParamStore::setDefaults(const Params &p)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_defaults = p;
    ++m_revision;
}

std::optional<Params> PlainRawParamStore::override(const std::string &key) const
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_overrides.find(key);
        if (it == m_overrides.end())
            return std::nullopt;
        return it->second;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void PlainRawParamStore::setOverride(const std::string &key, const Params &p)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_overrides[key] = p;
    ++m_revision;
}

void PlainRawParamStore::clearOverride(const std::string &key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_overrides.erase(key);
    ++m_revision;
}

void PlainRawParamStore::moveOverride(const std::string &fromKey, const std::string &toKey)
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (fromKey == toKey)
            return;
        const auto it = m_overrides.find(fromKey);
        if (it == m_overrides.end())
            return;
        const Params value = it->second;
        m_overrides[toKey] = value;
        m_overrides.erase(fromKey);
        ++m_revision;
    }
    catch (...)
    {
    }
}

void PlainRawParamStore::copyOverride(const std::string &fromKey, const std::string &toKey)
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (fromKey == toKey)
            return;
        const auto it = m_overrides.find(fromKey);
        if (it == m_overrides.end())
            return;
        const Params value = it->second;
        m_overrides[toKey] = value;
        ++m_revision;
    }
    catch (...)
    {
    }
}

void PlainRawParamStore::clearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_defaults = Params{};
    m_overrides.clear();
    ++m_revision;
}

uint64_t PlainRawParamStore::revision() const
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_revision;
    }
    catch (...)
    {
        return 0;
    }
}

namespace
{

// A per-file override is what the user picked in the dialog: never swap in a
// packing guess, only check it against the file size.
Resolved resolveOverride(const Params &p, int64_t fileSize)
{
    Resolved r;
    r.params = p;
    r.check = checkFileSize(p, fileSize);
    r.expectedBytes = frameBytes(p);
    r.extraBytes = 0;
    if (r.check == SizeCheck::Larger && r.expectedBytes >= 0 && fileSize >= r.expectedBytes)
        r.extraBytes = fileSize - r.expectedBytes;
    std::string reason;
    if (!isValid(p, &reason))
    {
        r.why = reason;
        return r;
    }
    r.known = r.check == SizeCheck::Exact || r.check == SizeCheck::Larger;
    if (!r.known)
        r.why = "file is smaller than one frame (expected " + std::to_string(r.expectedBytes) +
                " bytes, file has " + std::to_string(fileSize) + ")";
    return r;
}

} // namespace

PlainRawFileInfo resolvePlainRawFile(const std::string &path)
{
    try
    {
        const auto native = mviewer::core::pathFromUtf8(path);
        std::ifstream file(native, std::ios::binary);
        if (!file.is_open())
            return unopenable();
        bool sized = false;
        const int64_t fileSize = sizeOfFile(native, sized);
        if (!sized)
            return unopenable();
        const std::string name = lastComponent(path);
        const std::string key = plainRawParamKey(path);
        const auto saved = PlainRawParamStore::instance().override(key);
        PlainRawFileInfo info;
        info.fileSize = fileSize;
        info.fromOverride = saved.has_value();
        info.resolved =
            saved.has_value()
                ? resolveOverride(*saved, fileSize)
                : resolveForFile(PlainRawParamStore::instance().defaults(), name, fileSize);
        info.guesses = guessFromFileSize(info.resolved.params, fileSize);
        return info;
    }
    catch (...)
    {
        return unopenable();
    }
}

namespace
{

int shiftBlackLevel(int black, int oldBits, int newBits)
{
    if (newBits <= oldBits)
        return black;
    const int sh = newBits - oldBits;
    if (sh >= 31)
        return 0;
    const int64_t shifted = static_cast<int64_t>(black) << sh;
    if (shifted > static_cast<int64_t>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(shifted);
}

int containerDepth(int detected)
{
    if (detected <= 12)
        return 12;
    if (detected <= 14)
        return 14;
    return 16;
}

std::string depthExceededNote(int oldBits, int newBits, bool clipped)
{
    std::string text = "samples exceed ";
    text += std::to_string(oldBits);
    text += "-bit; ";
    if (clipped)
    {
        text += "values above white are clipped";
        return text;
    }
    text += "read as ";
    text += std::to_string(newBits);
    text += "-bit";
    return text;
}

void applyLsb16Depth(PlainRawFileInfo &info, const std::uint8_t *data, std::size_t size)
{
    info.depthNote.clear();
    Params &params = info.resolved.params;
    if (params.packing != Packing::Lsb16)
        return;
    const int detected = lsb16SignificantBits(params, data, size);
    if (detected <= params.bitDepth)
        return;
    const int oldBits = params.bitDepth;
    if (info.fromOverride)
    {
        info.depthNote = depthExceededNote(oldBits, oldBits, true);
        return;
    }
    const int neu = containerDepth(detected);
    params.blackLevel = shiftBlackLevel(params.blackLevel, oldBits, neu);
    params.whiteLevel = 0;
    params.bitDepth = neu;
    info.depthNote = depthExceededNote(oldBits, neu, false);
}

} // namespace

bool readPlainRawSamples(const std::string &path, PlainRawFileInfo &info,
                         std::vector<std::uint16_t> &samples, std::string *why)
{
    try
    {
        samples.clear();
        if (!info.resolved.known)
        {
            setWhy(why, info.resolved.why.empty() ? std::string("unknown raw frame")
                                                  : info.resolved.why);
            return false;
        }
        int64_t bytes = 0;
        std::string reason;
        if (!payloadSize(info.resolved.params, info.fileSize, bytes, reason))
        {
            setWhy(why, reason);
            return false;
        }
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(bytes));
        if (!readExact(path, info.resolved.params.headerOffset, raw))
        {
            setWhy(why, "short read");
            return false;
        }
        applyLsb16Depth(info, raw.data(), raw.size());
        if (!unpackPlainRaw(info.resolved.params, raw.data(), raw.size(), samples))
        {
            setWhy(why, "unpack failed");
            samples.clear();
            return false;
        }
        setWhy(why, std::string());
        return true;
    }
    catch (...)
    {
        samples.clear();
        setWhy(why, "exception while reading file");
        return false;
    }
}

ImageData decodePlainRawFile(const std::string &path, int maxEdge, PlainRawFileInfo *infoOut,
                             std::string *why)
{
    try
    {
        PlainRawFileInfo info = resolvePlainRawFile(path);
        if (!info.resolved.known)
        {
            if (infoOut != nullptr)
                *infoOut = info;
            setWhy(why, info.resolved.why);
            return {};
        }
        std::vector<std::uint16_t> samples;
        std::string reason;
        const bool read = readPlainRawSamples(path, info, samples, &reason);
        if (infoOut != nullptr)
            *infoOut = info;
        if (!read)
        {
            setWhy(why, reason);
            return {};
        }
        ImageData image = renderPlainRaw(info.resolved.params, samples, maxEdge);
        if (image.isNull())
        {
            setWhy(why, "render failed");
            return {};
        }
        setWhy(why, std::string());
        return image;
    }
    catch (...)
    {
        setWhy(why, "exception while decoding file");
        return {};
    }
}

void fillPlainRawMetadata(const PlainRawFileInfo &info, const std::string &path,
                          mviewer::domain::ImageMetadata &meta)
{
    try
    {
        const Params &p = info.resolved.params;
        meta.width = p.width;
        meta.height = p.height;
        meta.fileSize = info.fileSize < 0 ? int64_t{0} : info.fileSize;
        meta.fileName = lastComponent(path);
        meta.filePath = path;
        meta.bitDepth = p.bitDepth;
        meta.channels = (p.cfa == Cfa::Mono) ? 1 : 3;
        meta.format = "RAW (plain)";
        meta.colorSpace = std::string(cfaLabel(p.cfa)) + " " + std::to_string(p.bitDepth) + "-bit";
        if (!info.depthNote.empty())
        {
            meta.colorSpace += " (";
            meta.colorSpace += info.depthNote;
            meta.colorSpace += ")";
            meta.textKeys["MViewer.PlainRaw.Note"] = info.depthNote;
        }
        meta.frameCount = 1;
        meta.currentFrame = 0;
        meta.sequenceKind = "static";
        meta.animated = false;
    }
    catch (...)
    {
        meta = mviewer::domain::ImageMetadata{};
    }
}

std::string plainRawCacheKeySuffix(const std::string &path)
{
    try
    {
        if (!isPlainRawPath(path))
            return {};
        const std::string key = plainRawParamKey(path);
        const auto saved = PlainRawParamStore::instance().override(key);
        const Params params =
            saved.has_value() ? *saved : PlainRawParamStore::instance().defaults();
        return std::string("|praw=") + toString(params);
    }
    catch (...)
    {
        return {};
    }
}

bool plainRawCacheKeyAgrees(const std::string &key, const std::string &suffix)
{
    try
    {
        const std::string marker = "|praw=";
        if (suffix.empty())
            return key.find(marker) == std::string::npos;
        if (suffix.size() > key.size())
            return false;
        return key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    catch (...)
    {
        return false;
    }
}

std::string plainRawSampleText(const std::string &path, int x, int y)
{
    try
    {
        if (!isPlainRawPath(path))
            return {};
        PlainRawFileInfo info = resolvePlainRawFile(path);
        if (!info.resolved.known)
            return {};
        const std::string id = cacheIdentity(path, info.resolved.params, info.fileSize);
        {
            std::lock_guard<std::mutex> lock(sampleMutex());
            SampleCache &cache = sampleCache();
            if (cache.key == id)
                return sampleLine(cache.params, cache.samples, x, y);
        }
        std::vector<std::uint16_t> samples;
        if (!readPlainRawSamples(path, info, samples, nullptr))
            return {};
        const Params params = info.resolved.params;
        std::string text = sampleLine(params, samples, x, y);
        std::lock_guard<std::mutex> lock(sampleMutex());
        SampleCache &cache = sampleCache();
        cache.key = id;
        cache.params = params;
        cache.samples = std::move(samples);
        return text;
    }
    catch (...)
    {
        return {};
    }
}

} // namespace mviewer::core::rawplain
