#include "core/image/rawplain/PlainRawFile.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/rawplain/PlainRawRender.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mviewer::core::rawplain
{
namespace
{

constexpr std::size_t kSampleCacheSlots = 4;

struct SampleEntry
{
    std::string key;
    Params params;
    std::vector<std::uint16_t> samples;
};

struct SampleCache
{
    std::vector<SampleEntry> entries; // most recent first, at most kSampleCacheSlots
};

std::mutex &sampleMutex()
{
    static std::mutex mutex;
    return mutex;
}

// Guards the fill future only. The worker inserts under sampleMutex, so the
// launcher must not hold that mutex: std::async can run the task before it
// returns.
std::mutex &fillMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::future<void> &sampleFillSlot()
{
    static std::future<void> fill;
    return fill;
}

char sampleChannel(const Params &p, int x, int y)
{
    if (p.cfa == Cfa::Mono)
    {
        return 'Y';
    }
    const CfaColor colour = cfaColorAt(p.cfa, x, y);
    if (colour == CfaColor::Red)
    {
        return 'R';
    }
    if (colour == CfaColor::Blue)
    {
        return 'B';
    }
    return 'G';
}

std::string sampleLine(const Params &p, const std::vector<std::uint16_t> &samples, int x, int y)
{
    if (x < 0 || y < 0 || x >= p.width || y >= p.height)
    {
        return {};
    }
    const int64_t idx =
        static_cast<int64_t>(y) * static_cast<int64_t>(p.width) + static_cast<int64_t>(x);
    if (idx < 0 || static_cast<std::uint64_t>(idx) >= static_cast<std::uint64_t>(samples.size()))
    {
        return {};
    }
    const auto value = samples[static_cast<std::size_t>(idx)];
    std::string text = "RAW ";
    text.push_back(sampleChannel(p, x, y));
    text.push_back('=');
    text += std::to_string(static_cast<unsigned>(value));
    text += " (";
    text += std::to_string(p.bitDepth);
    text += "-bit)";
    return text;
}

std::int64_t lastWriteCount(const std::string &path)
{
    try
    {
        const auto native = mviewer::core::pathFromUtf8(path);
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(native, ec);
        if (ec)
        {
            return 0;
        }
        return static_cast<std::int64_t>(stamp.time_since_epoch().count());
    }
    catch (...)
    {
        return 0;
    }
}

std::string cacheIdentity(const std::string &path, const Params &p, int64_t fileSize)
{
    std::string key = plainRawParamKey(path);
    key.push_back('|');
    key += toString(p);
    key.push_back('|');
    key += std::to_string(fileSize);
    key.push_back('|');
    key += std::to_string(lastWriteCount(path));
    return key;
}

SampleCache &sampleCache()
{
    static SampleCache cache;
    return cache;
}

void promoteEntry(std::vector<SampleEntry> &entries, std::size_t index)
{
    if (index == 0 || index >= entries.size())
    {
        return;
    }
    SampleEntry hit = std::move(entries[index]);
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    entries.insert(entries.begin(), std::move(hit));
}

// Caller holds sampleMutex().
bool lookupSampleLocked(const std::string &id, int x, int y, std::string &text)
{
    auto &entries = sampleCache().entries;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].key != id)
        {
            continue;
        }
        promoteEntry(entries, i);
        text = sampleLine(entries.front().params, entries.front().samples, x, y);
        return true;
    }
    return false;
}

void storeSample(std::string id, Params params, std::vector<std::uint16_t> samples)
{
    std::lock_guard<std::mutex> lock(sampleMutex());
    auto &entries = sampleCache().entries;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].key != id)
        {
            continue;
        }
        entries[i].params = std::move(params);
        entries[i].samples = std::move(samples);
        promoteEntry(entries, i);
        return;
    }
    SampleEntry entry;
    entry.key = std::move(id);
    entry.params = std::move(params);
    entry.samples = std::move(samples);
    entries.insert(entries.begin(), std::move(entry));
    if (entries.size() > kSampleCacheSlots)
    {
        entries.pop_back();
    }
}

// Caller holds fillMutex().
bool sampleFillRunning()
{
    std::future<void> &fill = sampleFillSlot();
    if (!fill.valid())
    {
        return false;
    }
    return fill.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool fillSampleCache(const std::string &path)
{
    try
    {
        PlainRawFileInfo info = resolvePlainRawFile(path);
        if (!info.resolved.known)
        {
            return false;
        }
        // Identity uses the params from resolve. The read may raise bit depth;
        // the stored samples still answer later lookups of that same identity.
        const std::string id = cacheIdentity(path, info.resolved.params, info.fileSize);
        std::vector<std::uint16_t> samples;
        if (!readPlainRawSamples(path, info, samples, nullptr))
        {
            return false;
        }
        storeSample(id, info.resolved.params, std::move(samples));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void startSampleFill(const std::string &path)
{
    std::lock_guard<std::mutex> lock(fillMutex());
    if (sampleFillRunning())
    {
        return;
    }
    sampleFillSlot() = std::async(std::launch::async, [path]() { (void)fillSampleCache(path); });
}

std::string readAndStoreSample(const std::string &path, PlainRawFileInfo info,
                               const std::string &id, int x, int y)
{
    std::vector<std::uint16_t> samples;
    if (!readPlainRawSamples(path, info, samples, nullptr))
    {
        return {};
    }
    const std::string text = sampleLine(info.resolved.params, samples, x, y);
    storeSample(id, info.resolved.params, std::move(samples));
    return text;
}

} // namespace

std::string plainRawSampleText(const std::string &path, int x, int y, SampleRead mode)
{
    try
    {
        if (!isPlainRawPath(path))
        {
            return {};
        }
        PlainRawFileInfo info = resolvePlainRawFile(path);
        if (!info.resolved.known)
        {
            return {};
        }
        const std::string id = cacheIdentity(path, info.resolved.params, info.fileSize);
        bool hit = false;
        std::string text;
        {
            std::lock_guard<std::mutex> lock(sampleMutex());
            hit = lookupSampleLocked(id, x, y, text);
        }
        if (hit)
        {
            return text;
        }
        if (mode == SampleRead::CachedOrAsync)
        {
            startSampleFill(path);
            return {};
        }
        return readAndStoreSample(path, std::move(info), id, x, y);
    }
    catch (...)
    {
        return {};
    }
}

bool plainRawSampleFillIdle()
{
    try
    {
        std::lock_guard<std::mutex> lock(fillMutex());
        return !sampleFillRunning();
    }
    catch (...)
    {
        return true;
    }
}

} // namespace mviewer::core::rawplain
