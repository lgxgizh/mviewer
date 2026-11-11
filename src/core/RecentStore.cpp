//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "RecentStore.h"

#include "core/filesystem/AtomicFile.h"
#include "core/filesystem/Utf8Path.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstdint>
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

void stripTrailingCr(std::string &line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

std::string flagsPathFor(const std::string &recentsPath)
{
    const auto dir = pathFromUtf8(recentsPath).parent_path();
    return pathToUtf8((dir.empty() ? pathFromUtf8(".") : dir) / pathFromUtf8("flags.txt"));
}
} // namespace

RecentStore &RecentStore::instance()
{
    // Function-local static. The constructor does not touch disk: the path is
    // resolved on first use, after QCoreApplication exists (same deferral as
    // the app-data location).
    static RecentStore s;
    return s;
}

std::string RecentStore::defaultPath() const
{
#ifdef _WIN32
    std::string base = getEnvUtf8(L"LOCALAPPDATA");
    if (base.empty())
        base = getEnvUtf8(L"APPDATA");
    if (!base.empty())
        return base + "\\mviewer\\recents.txt";
#else
    std::string home = getEnv("HOME");
    if (!home.empty())
        return home + "/.mviewer/recents.txt";
#endif
    return "recents.txt";
}

void RecentStore::ensureLoaded() const
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_loaded)
        return;
    if (m_filePath.empty())
        m_filePath = resolved;
    if (m_filePath.empty())
        return;
    // Entries recorded before the path could be resolved stay in memory.
    if (!m_recents.empty())
    {
        m_loaded = true;
        return;
    }
    loadLocked();
}

void RecentStore::loadLocked() const
{
    m_loaded = true;
    m_recents.clear();
    if (m_filePath.empty())
        return;

    std::error_code ec;
    const bool haveFile = std::filesystem::exists(pathFromUtf8(m_filePath), ec) && !ec;
    if (haveFile)
    {
        std::ifstream in(pathFromUtf8(m_filePath), std::ios::binary);
        if (!in)
            return;
        std::string line;
        while (std::getline(in, line))
        {
            stripTrailingCr(line);
            if (line.empty())
                continue;
            const std::string key = normalizeKey(line);
            if (key.empty())
                continue;
            if (std::find(m_recents.begin(), m_recents.end(), key) != m_recents.end())
                continue;
            m_recents.push_back(key);
            if (static_cast<int>(m_recents.size()) >= kMaxRecents)
                break;
        }
        return;
    }

    // One-time migration from the legacy flags.txt. Read only "N|<path>"
    // lines, in file order. Never modify or delete flags.txt.
    const std::string flagsPath = flagsPathFor(m_filePath);
    if (!std::filesystem::exists(pathFromUtf8(flagsPath), ec) || ec)
        return;
    std::ifstream in(pathFromUtf8(flagsPath), std::ios::binary);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line))
    {
        stripTrailingCr(line);
        if (line.size() < 2 || line[0] != 'N' || line[1] != '|')
            continue;
        const std::string key = normalizeKey(line.substr(2));
        if (key.empty())
            continue;
        if (std::find(m_recents.begin(), m_recents.end(), key) != m_recents.end())
            continue;
        m_recents.push_back(key);
        if (static_cast<int>(m_recents.size()) >= kMaxRecents)
            break;
    }
    in.close();

    const std::string body = serializeLocked();
    std::string error;
    if (!atomicWriteFile(m_filePath, body, &error))
        m_dirty = true;
}

std::string RecentStore::serializeLocked() const
{
    std::ostringstream out;
    for (const auto &path : m_recents)
        out << path << '\n';
    return out.str();
}

void RecentStore::addRecent(const std::string &path)
{
    ensureLoaded();
    const std::string key = normalizeKey(path);
    if (key.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find(m_recents.begin(), m_recents.end(), key);
        if (it != m_recents.end())
            m_recents.erase(it);
        m_recents.insert(m_recents.begin(), key);
        while (static_cast<int>(m_recents.size()) > kMaxRecents)
            m_recents.pop_back();
    }
    markDirty();
}

std::vector<std::string> RecentStore::recents() const
{
    ensureLoaded();
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recents;
}

bool RecentStore::isRecent(const std::string &path) const
{
    ensureLoaded();
    const std::string key = normalizeKey(path);
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::find(m_recents.begin(), m_recents.end(), key) != m_recents.end();
}

void RecentStore::renamePath(const std::string &oldPath, const std::string &newPath)
{
    ensureLoaded();
    const std::string from = normalizeKey(oldPath);
    const std::string to = normalizeKey(newPath);
    if (from.empty() || to.empty() || from == to)
        return;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto recent = std::find(m_recents.begin(), m_recents.end(), from);
        if (recent == m_recents.end())
            return;
        const bool targetListed =
            std::find(m_recents.begin(), m_recents.end(), to) != m_recents.end();
        if (targetListed)
            m_recents.erase(recent);
        else
            *recent = to;
        changed = true;
    }
    if (changed)
        markDirty();
}

size_t RecentStore::pruneMissing()
{
    ensureLoaded();
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        keys = m_recents;
    }
    std::unordered_set<std::string> missing;
    missing.reserve(keys.size());
    for (const std::string &key : keys)
    {
        std::error_code ec;
        if (!std::filesystem::exists(pathFromUtf8(key), ec) || ec)
            missing.insert(key);
    }
    if (missing.empty())
        return 0;

    size_t removed = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> kept;
        kept.reserve(m_recents.size());
        for (const auto &path : m_recents)
        {
            if (missing.count(path) != 0)
                ++removed;
            else
                kept.push_back(path);
        }
        m_recents.swap(kept);
    }
    if (removed > 0)
        markDirty();
    return removed;
}

void RecentStore::markDirty()
{
    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dirty = true;
        ++m_epoch;
        if (!m_saveQueued && !m_filePath.empty())
        {
            m_saveQueued = true;
            schedule = true;
        }
    }
    if (schedule)
        enqueueSave();
}

void RecentStore::enqueueSave()
{
    // One in-flight background save. Further addRecent calls only set dirty;
    // the running save writes the latest list, then reschedules if needed.
    auto handle = TaskScheduler::instance().submit(TaskScheduler::Priority::Background,
                                                   [this](const TaskScheduler::TaskContext &)
                                                   { backgroundSave(); });
    if (!handle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_saveQueued = false;
    }
}

void RecentStore::backgroundSave()
{
    const bool ok = writeDirty();
    bool again = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ok && m_dirty && !m_filePath.empty())
            again = true;
        else
            m_saveQueued = false;
    }
    if (again)
        enqueueSave();
}

bool RecentStore::writeDirty()
{
    std::lock_guard<std::mutex> writeLock(m_writeMutex);
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        std::string path;
        std::string body;
        std::uint64_t epoch = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_dirty)
                return true;
            if (m_filePath.empty())
                return false;
            path = m_filePath;
            body = serializeLocked();
            epoch = m_epoch;
            m_dirty = false;
        }
        std::string error;
        const bool ok = atomicWriteFile(path, body, &error);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!ok)
        {
            m_dirty = true;
            return false;
        }
        if (m_epoch == epoch)
            return true;
        // A newer edit landed during the write. Persist that snapshot.
        m_dirty = true;
    }
    return false;
}

bool RecentStore::flush()
{
    // Wait for an in-flight background write before checking dirty.
    {
        std::lock_guard<std::mutex> writeLock(m_writeMutex);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_dirty)
            return true;
        if (m_filePath.empty() && QCoreApplication::instance() != nullptr)
            m_filePath = defaultPath();
    }
    return writeDirty();
}

void RecentStore::setFilePath(const std::string &path)
{
    (void)flush();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_filePath = path;
        m_loaded = false;
        m_recents.clear();
        m_dirty = false;
    }
    ensureLoaded();
}

} // namespace mviewer::core
