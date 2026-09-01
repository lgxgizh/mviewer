//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "TagStore.h"

#include "core/filesystem/AtomicFile.h"
#include "core/filesystem/Utf8Path.h"

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
    m_filePath = defaultPath();
    load();
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

std::string TagStore::defaultPath() const
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
    return "tags.txt";
}

std::string TagStore::normalize(const std::string &path) const
{
    std::string out = path;
    for (char &c : out)
        if (c == '\\')
            c = '/';
    return out;
}

std::vector<std::string> TagStore::tags(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_tags.find(normalize(path));
    if (it == m_tags.end())
        return {};
    return {it->second.begin(), it->second.end()};
}

bool TagStore::hasTag(const std::string &path, const std::string &tag) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_tags.find(normalize(path));
    if (it == m_tags.end())
        return false;
    return it->second.count(tag) > 0;
}

void TagStore::addTag(const std::string &path, const std::string &tag)
{
    if (tag.empty())
        return;
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tags[key].insert(tag);
    }
    scheduleSave();
}

void TagStore::removeTag(const std::string &path, const std::string &tag)
{
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
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::set<std::string> s(tags.begin(), tags.end());
        if (s.empty())
            m_tags.erase(key);
        else
            m_tags[key] = std::move(s);
    }
    scheduleSave();
}

void TagStore::clearTags(const std::string &path)
{
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tags.erase(key);
    }
    scheduleSave();
}

std::vector<std::string> TagStore::allTags() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::set<std::string> all;
    for (const auto &[p, ts] : m_tags)
        all.insert(ts.begin(), ts.end());
    return {all.begin(), all.end()};
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
    }
    load();
}

bool TagStore::Snapshot::hasTag(const std::string &path, const std::string &tag) const
{
    auto it = tags.find(path);
    return it != tags.end() && it->second.count(tag) != 0;
}

TagStore::Snapshot TagStore::snapshot() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    Snapshot out;
    out.tags = m_tags;
    return out;
}

bool TagStore::load()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_tags.clear();
    std::ifstream in(pathFromUtf8(m_filePath), std::ios::binary);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        // Format: "<tag>|<path>"
        const auto pos = line.find('|');
        if (pos == std::string::npos)
            continue;
        const std::string tag = line.substr(0, pos);
        const std::string p = line.substr(pos + 1);
        if (tag.empty() || p.empty())
            continue;
        m_tags[p].insert(tag);
    }
    return true;
}

bool TagStore::save() const
{
    bool shouldWrite = false;
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        shouldWrite = m_dirty;
        if (shouldWrite)
        {
            m_dirty = false;
            m_workerCv.notify_all();
        }
    }
    // Wait for a worker write already in progress before returning.
    std::lock_guard<std::mutex> writeLock(m_writeMutex);
    if (!shouldWrite)
        return true;
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
    std::string path;
    std::map<std::string, std::set<std::string>> tags;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_filePath;
        tags = m_tags;
    }
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
                lock, std::chrono::milliseconds(100),
                [this, observed]
                {
                    return m_workerStop || !m_dirty || m_changeSerial != observed;
                });
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
