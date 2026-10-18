//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "TagStore.h"

#include "core/filesystem/AtomicFile.h"
#include "core/filesystem/Utf8Path.h"
#include "runtime_storage.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace mviewer::core
{

namespace
{
std::string getEnv(const char *name)
{
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

#ifdef _WIN32
std::string getEnvUtf8(const wchar_t *name)
{
    const wchar_t *v = _wgetenv(name);
    return v ? pathToUtf8(std::filesystem::path(v)) : std::string();
}
#endif
} // namespace

TagStore::TagStore()
{
    // Do not resolve the store path here. instance() is a function-local
    // static, so this constructor can run before QCoreApplication exists
    // and before the application name (which QStandardPaths needs) is set.
    m_worker = std::thread([this]() { workerLoop(); });
}

TagStore::~TagStore()
{
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerStop = true;
    }
    m_workerCv.notify_one();
    if (m_worker.joinable())
        m_worker.join();
}

TagStore &TagStore::instance()
{
    static TagStore s;
    return s;
}

std::string TagStore::legacyPath() const
{
#ifdef _WIN32
    std::string base = getEnvUtf8(L"LOCALAPPDATA");
    if (base.empty())
        base = getEnvUtf8(L"APPDATA");
    if (!base.empty())
        return base + "\\mviewer\\tags.txt";
#else
    std::string home = getEnv("HOME");
    if (!home.empty())
        return home + "/.mviewer/tags.txt";
#endif
    return {};
}

std::string TagStore::defaultPath() const
{
    // Writable app-data dir. Requires QCoreApplication so the org/app name
    // is part of the path. Empty when called too early.
    if (QCoreApplication::instance() == nullptr)
        return {};
    const QString modern =
        mviewer::runtime::filePath(QStandardPaths::AppDataLocation, QStringLiteral("tags.txt"));
    if (modern.isEmpty())
    {
        std::string legacy = legacyPath();
        if (legacy.empty())
            return "tags.txt";
        return legacy;
    }
    if (QFileInfo::exists(modern))
        return modern.toUtf8().toStdString();
    std::string legacy = legacyPath();
    if (!legacy.empty())
    {
        std::error_code ec;
        if (std::filesystem::exists(pathFromUtf8(legacy), ec))
            return legacy;
    }
    const QByteArray utf8 = modern.toUtf8();
    if (utf8.isEmpty())
        return {};
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

namespace
{
// RatingStore / TagStore path identity. One normalizer for load and lookup.
std::string slashKey(const std::string &path)
{
    return normalizeKey(path);
}

void stripTrailingCr(std::string &line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

// '|' separates tag from path and '\n' separates records. Either character
// in the tag text would split the line on the next load.
bool tagAcceptable(const std::string &tag)
{
    return !tag.empty() && tag.find('|') == std::string::npos &&
           tag.find('\n') == std::string::npos && tag.find('\r') == std::string::npos;
}

// Snapshot lookups accept both the stored (normalized) key and a caller's
// verbatim slash-form key, so snapshots assembled outside the store keep working.
template <typename Container> auto findPathKey(const Container &c, const std::string &path)
{
    std::string raw = path;
    for (char &ch : raw)
    {
        if (ch == '\\')
            ch = '/';
    }
    auto it = c.find(raw);
    return it != c.end() ? it : c.find(slashKey(path));
}
} // namespace

std::string TagStore::normalize(const std::string &path) const
{
    return slashKey(path);
}

std::vector<std::string> TagStore::tags(const std::string &path) const
{
    ensureReady();
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_tags.find(normalize(path));
    if (it == m_tags.end())
        return {};
    return {it->second.begin(), it->second.end()};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool TagStore::hasTag(const std::string &path, const std::string &tag) const
{
    ensureReady();
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_tags.find(normalize(path));
    if (it == m_tags.end())
        return false;
    return it->second.count(tag) > 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void TagStore::addTag(const std::string &path, const std::string &tag)
{
    if (!tagAcceptable(tag))
        return;
    ensureReady();
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tags[key].insert(tag);
    }
    scheduleSave();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void TagStore::removeTag(const std::string &path, const std::string &tag)
{
    ensureReady();
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_tags.find(key);
        if (it == m_tags.end())
            return;
        it->second.erase(tag);
        if (it->second.empty())
            m_tags.erase(it);
    }
    scheduleSave();
}

void TagStore::setTags(const std::string &path, const std::vector<std::string> &tags)
{
    std::set<std::string> accepted;
    for (const std::string &tag : tags)
    {
        if (tagAcceptable(tag))
            accepted.insert(tag);
    }
    // A call whose every tag was illegal must not wipe the stored set.
    // An empty input still clears, matching the previous contract.
    if (accepted.empty() && !tags.empty())
        return;
    ensureReady();
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (accepted.empty())
            m_tags.erase(key);
        else
            m_tags[key] = std::move(accepted);
    }
    scheduleSave();
}

void TagStore::clearTags(const std::string &path)
{
    ensureReady();
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tags.erase(key);
    }
    scheduleSave();
}

std::vector<std::string> TagStore::allTags() const
{
    ensureReady();
    std::lock_guard<std::mutex> lk(m_mutex);
    std::set<std::string> all;
    for (const auto &[p, ts] : m_tags)
        all.insert(ts.begin(), ts.end());
    return {all.begin(), all.end()};
}

bool TagStore::renamePath(const std::string &oldPath, const std::string &newPath)
{
    ensureReady();
    const std::string from = normalize(oldPath);
    const std::string to = normalize(newPath);
    if (from == to)
        return false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_tags.find(from);
        if (it == m_tags.end())
            return false;
        auto tags = std::move(it->second);
        m_tags.erase(it);
        m_tags[to] = std::move(tags);
    }
    scheduleSave();
    return true;
}

size_t TagStore::pruneMissing()
{
    ensureReady();
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        keys.reserve(m_tags.size());
        for (const auto &entry : m_tags)
            keys.push_back(entry.first);
    }

    // Stat outside the store mutex. exists() can block on a network volume.
    std::unordered_set<std::string> missing;
    missing.reserve(keys.size());
    for (const std::string &key : keys)
    {
        std::error_code ec;
        if (!std::filesystem::exists(pathFromUtf8(key), ec))
            missing.insert(key);
    }

    size_t removed = 0;
    if (!missing.empty())
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const std::string &key : missing)
        {
            auto it = m_tags.find(key);
            if (it == m_tags.end())
                continue;
            removed += it->second.size();
            m_tags.erase(it);
        }
    }

    if (removed > 0)
        scheduleSave();
    return removed;
}

void TagStore::setFilePath(const std::string &path)
{
    flushSave();
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_dirty = false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_filePath = path;
        m_loaded = false;
    }
    load();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool TagStore::Snapshot::hasTag(const std::string &path, const std::string &tag) const
{
    auto it = findPathKey(tags, path);
    return it != tags.end() && it->second.count(tag) != 0;
}

TagStore::Snapshot TagStore::snapshot() const
{
    ensureReady();
    std::lock_guard<std::mutex> lk(m_mutex);
    Snapshot out;
    out.tags = m_tags;
    return out;
}

void TagStore::ensureReady() const
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_loaded)
            return;
        if (!m_filePath.empty())
        {
            loadLocked();
            return;
        }
    }
    if (QCoreApplication::instance() == nullptr)
        return;
    const std::string resolved = defaultPath();
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_loaded)
        return;
    if (m_filePath.empty())
        m_filePath = resolved;
    if (m_filePath.empty())
        return;
    // Tags inserted before the path could be resolved stay in memory.
    // Loading would drop them. The normal startup path has an empty map.
    if (!m_tags.empty())
    {
        m_loaded = true;
        return;
    }
    loadLocked();
}

void TagStore::loadLocked() const
{
    m_tags.clear();
    m_loaded = true;
    if (m_filePath.empty())
        return;
    std::ifstream in(pathFromUtf8(m_filePath), std::ios::binary);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line))
    {
        stripTrailingCr(line);
        if (line.empty())
            continue;
        // Format: "<tag>|<path>"
        const auto pos = line.find('|');
        if (pos == std::string::npos)
            continue;
        const std::string tag = line.substr(0, pos);
        if (!tagAcceptable(tag))
            continue;
        const std::string p = normalize(line.substr(pos + 1));
        if (p.empty())
            continue;
        m_tags[p].insert(tag);
    }
}

bool TagStore::load()
{
    std::string resolved;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        resolved = m_filePath;
    }
    if (resolved.empty())
    {
        if (QCoreApplication::instance() == nullptr)
            return false;
        resolved = defaultPath();
        if (resolved.empty())
            return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_filePath.empty())
            m_filePath = resolved;
    }
    bool existed = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_filePath.empty())
            return false;
        std::ifstream probe(pathFromUtf8(m_filePath), std::ios::binary);
        existed = static_cast<bool>(probe);
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    loadLocked();
    return existed;
}

bool TagStore::save() const
{
    // Always persist the latest snapshot once the write lock is held. Bailing
    // out because m_dirty is already false drops the edit when the worker has
    // cleared the bit but has not yet written (the flags-store race).
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        if (m_dirty)
        {
            m_dirty = false;
            m_workerCv.notify_all();
        }
    }
    std::lock_guard<std::mutex> writeLock(m_writeMutex);
    const bool ok = saveSnapshot();
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_dirty = true;
    }
    return ok;
}

void TagStore::flushSave()
{
    (void)save();
}

bool TagStore::saveSnapshot() const
{
    // M46: crash-safe atomic replace (temp -> flush/close -> replace). A
    // failed write leaves the previous official tags file intact; the file
    // can never be observed half-written. Snapshot state under the short
    // mutex, then do filesystem work without blocking readers or editors.
    ensureReady();
    std::string path;
    std::map<std::string, std::set<std::string>> tags;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_filePath;
        tags = m_tags;
    }
    if (path.empty())
        return true;
    std::ostringstream buf;
    for (const auto &[p, ts] : tags)
        for (const auto &t : ts)
            buf << t << '|' << p << '\n';
    std::string error;
    return atomicWriteFile(path, buf.str(), &error);
}

void TagStore::scheduleSave()
{
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_dirty = true;
        ++m_changeSerial;
    }
    m_workerCv.notify_one();
}

void TagStore::workerLoop()
{
    std::unique_lock<std::mutex> lock(m_workerMutex);
    for (;;)
    {
        if (!m_dirty && !m_workerStop)
            m_workerCv.wait(lock, [this] { return m_dirty || m_workerStop; });
        if (m_workerStop && !m_dirty)
            return;

        uint64_t observed = m_changeSerial;
        bool write = m_workerStop;
        while (!write && m_dirty)
        {
            const bool woke = m_workerCv.wait_for(
                lock, std::chrono::milliseconds(100), [this, observed]
                { return m_workerStop || !m_dirty || m_changeSerial != observed; });
            if (!woke)
            {
                write = true;
                break;
            }
            if (m_workerStop)
            {
                write = true;
                break;
            }
            if (!m_dirty)
                break; // save() owns the synchronous write
            observed = m_changeSerial;
        }
        if (!write)
            continue;

        m_dirty = false;
        lock.unlock();
        bool ok = false;
        {
            std::lock_guard<std::mutex> writeLock(m_writeMutex);
            ok = saveSnapshot();
        }
        lock.lock();
        if (m_workerStop)
        {
            m_dirty = false;
            return;
        }
        if (!ok)
            m_dirty = true;
    }
}

} // namespace mviewer::core
