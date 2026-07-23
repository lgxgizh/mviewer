//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "RatingStore.h"

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

RatingStore::RatingStore()
{
    m_filePath = defaultPath();
    load();
    m_flagsPath = flagsPath();
    loadFlags();
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
    save();
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
    saveFlags();
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
    saveFlags();
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
    saveFlags();
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
    saveFlags();
}

std::vector<std::string> RatingStore::favorites() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return std::vector<std::string>(m_picked.begin(), m_picked.end());
}

void RatingStore::setFilePath(const std::string &path)
{
    m_filePath = path;
    m_flagsPath = flagsPath();
    load();
    loadFlags();
}

void RatingStore::saveFlags() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::error_code ec;
    const auto dir = std::filesystem::path(m_flagsPath).parent_path();
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(m_flagsPath, std::ios::trunc);
    if (!out)
        return;
    for (const auto &[p, n] : m_colorLabels)
        if (n > 0)
            out << "L|" << p << '|' << n << '\n';
    for (const auto &p : m_rejected)
        out << "X|" << p << '\n';
    for (const auto &p : m_picked)
        out << "K|" << p << '\n';
    for (const auto &p : m_recents)
        out << "N|" << p << '\n';
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

bool RatingStore::save() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::error_code ec;
    const auto dir = std::filesystem::path(m_filePath).parent_path();
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(m_filePath, std::ios::trunc);
    if (!out)
        return false;
    for (const auto &[p, s] : m_ratings)
        out << s << '|' << p << '\n';
    return true;
}

} // namespace mviewer::core
