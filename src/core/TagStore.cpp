//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "TagStore.h"

#include <algorithm>
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

TagStore::TagStore()
{
    m_filePath = defaultPath();
    load();
}

TagStore &TagStore::instance()
{
    static TagStore s;
    return s;
}

std::string TagStore::defaultPath() const
{
#ifdef _WIN32
    std::string base = getEnv("LOCALAPPDATA");
    if (base.empty())
        base = getEnv("APPDATA");
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
    save();
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
    save();
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
    save();
}

void TagStore::clearTags(const std::string &path)
{
    const std::string key = normalize(path);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tags.erase(key);
    }
    save();
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
    m_filePath = path;
    load();
}

bool TagStore::load()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_tags.clear();
    std::ifstream in(m_filePath);
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
    std::lock_guard<std::mutex> lk(m_mutex);
    std::error_code ec;
    const auto dir = std::filesystem::path(m_filePath).parent_path();
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(m_filePath, std::ios::trunc);
    if (!out)
        return false;
    for (const auto &[p, ts] : m_tags)
        for (const auto &t : ts)
            out << t << '|' << p << '\n';
    return true;
}

} // namespace mviewer::core
