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

namespace mviewer::core {

namespace {
std::string getEnv(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}
}  // namespace

RatingStore::RatingStore()
{
    m_filePath = defaultPath();
    load();
}

RatingStore& RatingStore::instance()
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

std::string RatingStore::normalize(const std::string& path) const
{
    std::string out = path;
    for (char& c : out)
        if (c == '\\')
            c = '/';
    return out;
}

int RatingStore::rating(const std::string& path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_ratings.find(normalize(path));
    return it == m_ratings.end() ? 0 : it->second;
}

bool RatingStore::hasRating(const std::string& path) const
{
    return rating(path) > 0;
}

void RatingStore::setRating(const std::string& path, int stars)
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

void RatingStore::clearRating(const std::string& path)
{
    setRating(path, 0);
}

void RatingStore::setFilePath(const std::string& path)
{
    m_filePath = path;
    load();
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
    for (const auto& [p, s] : m_ratings)
        out << s << '|' << p << '\n';
    return true;
}

}  // namespace mviewer::core
