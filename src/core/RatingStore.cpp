//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "RatingStore.h"

#include "core/filesystem/AtomicFile.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mviewer::core
{

namespace
{
std::string getEnv(const char *name)
{
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
}
} // namespace

RatingStore::RatingStore()
{
    m_filePath = defaultPath();
    load();
    m_flagsPath = flagsPath();
    loadFlags();
    m_flagsWorker = std::thread([this]() { flagsWorkerLoop(); });
}

RatingStore::~RatingStore()
{
    {
        std::lock_guard<std::mutex> lk(m_flagsWorkerMutex);
        m_flagsWorkerStop = true;
    }
    m_flagsWorkerCv.notify_one();
    if (m_flagsWorker.joinable())
        m_flagsWorker.join();
}

RatingStore &RatingStore::instance()
{
    static RatingStore s;
    return s;
}

std::string RatingStore::defaultPath() const
{
#ifdef _WIN32
    std::string base = getEnv("LOCALAPPDATA");
    if (base.empty())
        base = getEnv("APPDATA");
    if (!base.empty())
        return base + "\\mviewer\\ratings.txt";
#else
    std::string home = getEnv("HOME");
    if (!home.empty())
        return home + "/.mviewer/ratings.txt";
#endif
    return "ratings.txt";
}

std::string RatingStore::flagsPath() const
{
    const auto dir = std::filesystem::path(m_filePath).parent_path();
    const std::string d = dir.empty() ? "." : dir.string();
    return d + "/flags.txt";
}

std::string RatingStore::normalize(const std::string &path) const
{
    std::string out = path;
    for (char &c : out)
        if (c == '\\')
            c = '/';
    return out;
}

int RatingStore::rating(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_ratings.find(normalize(path));
    return it == m_ratings.end() ? 0 : it->second;
}

bool RatingStore::hasRating(const std::string &path) const
{
    return rating(path) > 0;
}

void RatingStore::setRating(const std::string &path, int stars)
{
    stars = std::clamp(stars, 0, 5);
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (stars <= 0)
            m_ratings.erase(key);
        else
            m_ratings[key] = stars;
    }
    // M46: user edits are coalesced on the owned worker (same debounce as
    // flags/recents) instead of a synchronous full-file rewrite on the calling
    // (UI) thread. Reads are still immediate; flushSave()/the destructor own
    // the explicit flush boundary, so no last edit is ever lost.
    scheduleSave();
}

void RatingStore::clearRating(const std::string &path)
{
    setRating(path, 0);
}

int RatingStore::colorLabel(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_colorLabels.find(normalize(path));
    return it == m_colorLabels.end() ? 0 : it->second;
}

bool RatingStore::hasColorLabel(const std::string &path) const
{
    return colorLabel(path) > 0;
}

void RatingStore::setColorLabel(const std::string &path, int label)
{
    label = std::clamp(label, 0, 6);
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (label <= 0)
            m_colorLabels.erase(key);
        else
            m_colorLabels[key] = label;
    }
    scheduleSave();
}

void RatingStore::clearColorLabel(const std::string &path)
{
    setColorLabel(path, 0);
}

bool RatingStore::rejected(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_rejected.count(normalize(path)) > 0;
}

void RatingStore::setRejected(const std::string &path, bool v)
{
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (v)
            m_rejected.insert(key);
        else
            m_rejected.erase(key);
    }
    scheduleSave();
}

bool RatingStore::picked(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_picked.count(normalize(path)) > 0;
}

void RatingStore::setPicked(const std::string &path, bool v)
{
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (v)
            m_picked.insert(key);
        else
            m_picked.erase(key);
    }
    scheduleSave();
}

std::vector<std::string> RatingStore::recents() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_recents;
}

void RatingStore::addRecent(const std::string &path)
{
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = std::find(m_recents.begin(), m_recents.end(), key);
        if (it != m_recents.end())
            m_recents.erase(it);
        m_recents.insert(m_recents.begin(), key);
        while (static_cast<int>(m_recents.size()) > kMaxRecents)
            m_recents.pop_back();
    }
    // Recents are browse telemetry, not an explicit edit. Keep the memory
    // order immediate and coalesce the compatible write in a single owned
    // worker. Explicit rating/flag edits share the same coalesced path.
    scheduleSave();
}

std::vector<std::string> RatingStore::favorites() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return std::vector<std::string>(m_picked.begin(), m_picked.end());
}

void RatingStore::setFilePath(const std::string &path)
{
    flushSave();
    {
        std::lock_guard<std::mutex> workerLock(m_flagsWorkerMutex);
        m_flagsDirty = false;
    }
    m_filePath = path;
    m_flagsPath = flagsPath();
    load();
    loadFlags();
}

bool RatingStore::save()
{
    // M46: explicit flush boundary. Drains any pending worker write and
    // writes the LATEST snapshot synchronously — the caller can rely on the
    // complete current state being on disk when this returns.
    bool shouldWrite = false;
    {
        std::lock_guard<std::mutex> lk(m_flagsWorkerMutex);
        shouldWrite = m_flagsDirty;
        if (shouldWrite)
        {
            m_flagsDirty = false;
            // Wake a worker that is inside the quiet period so it cannot
            // write a stale snapshot after this flush.
            m_flagsWorkerCv.notify_all();
        }
    }
    // Also wait for a worker write already in progress. This makes the flush
    // boundary a real boundary, not just a dirty-bit test.
    std::lock_guard<std::mutex> writeLock(m_flagsWriteMutex);
    if (shouldWrite)
        return saveSnapshot();
    return true;
}

void RatingStore::flushSave()
{
    (void)save();
}

bool RatingStore::saveSnapshot() const
{
    // Snapshot the complete state under the state mutex, then write each
    // file through the crash-safe atomic replace helper (temp -> flush/close
    // -> replace). A failed write leaves the previous official file intact;
    // both files are individually atomic, and the snapshot is a consistent
    // point-in-time view of every field.
    std::string ratingsPath;
    std::string flagsPath;
    std::map<std::string, int> labels;
    std::set<std::string> rejected;
    std::set<std::string> picked;
    std::vector<std::string> recents;
    std::map<std::string, int> ratings;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ratingsPath = m_filePath;
        flagsPath = m_flagsPath;
        labels = m_colorLabels;
        rejected = m_rejected;
        picked = m_picked;
        recents = m_recents;
        ratings = m_ratings;
    }

    std::ostringstream flagsBuf;
    for (const auto &[p, n] : labels)
        if (n > 0)
            flagsBuf << "L|" << p << '|' << n << '\n';
    for (const auto &p : rejected)
        flagsBuf << "X|" << p << '\n';
    for (const auto &p : picked)
        flagsBuf << "K|" << p << '\n';
    for (const auto &p : recents)
        flagsBuf << "N|" << p << '\n';

    std::ostringstream ratingsBuf;
    for (const auto &[p, s] : ratings)
        ratingsBuf << s << '|' << p << '\n';

    std::string error;
    bool ok = atomicWriteFile(ratingsPath, ratingsBuf.str(), &error);
    if (!atomicWriteFile(flagsPath, flagsBuf.str(), &error) && ok)
        ok = false;
    return ok;
}

void RatingStore::scheduleSave()
{
    {
        std::lock_guard<std::mutex> lk(m_flagsWorkerMutex);
        m_flagsDirty = true;
        ++m_flagsChangeSerial;
    }
    m_flagsWorkerCv.notify_one();
}

void RatingStore::flagsWorkerLoop()
{
    std::unique_lock<std::mutex> lk(m_flagsWorkerMutex);
    for (;;)
    {
        if (!m_flagsDirty && !m_flagsWorkerStop)
            m_flagsWorkerCv.wait(lk, [this] { return m_flagsDirty || m_flagsWorkerStop; });
        if (m_flagsWorkerStop && !m_flagsDirty)
            return;

        // Debounce a burst of A -> B -> C selections. Keep the dirty bit set
        // while waiting so flushSave() can synchronously cancel this quiet
        // period; the serial distinguishes a new change from the notification
        // generated by the flush itself.
        uint64_t observed = m_flagsChangeSerial;
        bool write = m_flagsWorkerStop;
        while (!write && m_flagsDirty)
        {
            const bool woke = m_flagsWorkerCv.wait_for(
                lk, std::chrono::milliseconds(100),
                [this, observed]
                {
                    return m_flagsWorkerStop || !m_flagsDirty ||
                           m_flagsChangeSerial != observed;
                });
            if (!woke)
            {
                write = true; // quiet period completed
                break;
            }
            if (m_flagsWorkerStop)
            {
                write = true; // shutdown flushes the latest snapshot
                break;
            }
            if (!m_flagsDirty)
                break; // flushSave() owns the write
            observed = m_flagsChangeSerial; // restart the quiet period
        }
        if (!write)
            continue;
        m_flagsDirty = false;
        lk.unlock();
        bool ok = false;
        {
            std::lock_guard<std::mutex> writeLock(m_flagsWriteMutex);
            ok = saveSnapshot();
        }
        lk.lock();
        if (m_flagsWorkerStop)
        {
            // Shutdown: one final write attempt was made; leave even on
            // failure (the official file was never corrupted, and the caller
            // can observe the failure through save()).
            m_flagsDirty = false;
            return;
        }
        if (!ok)
        {
            // M46: a failed write must NEVER silently drop the latest user
            // state. Re-arm the dirty bit so the next quiet period retries;
            // the retry cadence is the same 100 ms debounce.
            m_flagsDirty = true;
        }
    }
}

void RatingStore::loadFlags()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_colorLabels.clear();
    m_rejected.clear();
    m_picked.clear();
    m_recents.clear();
    std::ifstream in(m_flagsPath);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.size() < 2)
            continue;
        const char type = line[0];
        const auto pos = line.find('|');
        if (pos == std::string::npos)
            continue;
        const std::string p = line.substr(pos + 1);
        if (p.empty())
            continue;
        if (type == 'L')
        {
            const auto pos2 = p.find('|');
            if (pos2 == std::string::npos)
                continue;
            const std::string key = p.substr(0, pos2);
            int n = 0;
            try
            {
                n = std::stoi(p.substr(pos2 + 1));
            }
            catch (...)
            {
                continue;
            }
            if (n >= 1 && n <= 6)
                m_colorLabels[key] = n;
        }
        else if (type == 'X')
            m_rejected.insert(p);
        else if (type == 'K')
            m_picked.insert(p);
        else if (type == 'N')
            m_recents.push_back(p);
    }
}

bool RatingStore::load()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_ratings.clear();
    std::ifstream in(m_filePath);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        // Format: "<stars>|<path>"
        const auto pos = line.find('|');
        if (pos == std::string::npos)
            continue;
        int stars = 0;
        try
        {
            stars = std::stoi(line.substr(0, pos));
        }
        catch (...)
        {
            continue;
        }
        if (stars < 0 || stars > 5)
            continue;
        const std::string p = line.substr(pos + 1);
        if (!p.empty())
            m_ratings[p] = stars;
    }
    return true;
}

} // namespace mviewer::core
