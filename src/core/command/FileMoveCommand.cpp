#include "FileMoveCommand.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

FileMoveCommand::FileMoveCommand(std::vector<std::string> paths, std::string destDir)
    : m_paths(std::move(paths)), m_destDir(std::move(destDir))
{
}

std::string FileMoveCommand::description() const
{
    if (m_paths.size() == 1)
        return "Move " + fs::path(m_paths.front()).filename().string();
    return "Move " + std::to_string(m_paths.size()) + " files";
}

void FileMoveCommand::execute()
{
    if (!canExecute())
        return;
    m_moved.clear();
    m_lastError.clear();
    std::error_code ec;
    fs::create_directories(m_destDir, ec);
    if (ec)
    {
        m_lastError = "Cannot create destination directory: " + m_destDir;
        return;
    }
    for (const auto &p : m_paths)
    {
        fs::path src(p);
        if (!fs::exists(src, ec))
        {
            rollback();
            m_lastError = "Source file no longer exists: " + p;
            return;
        }
        fs::path dest = fs::path(m_destDir) / src.filename();
        int n = 0;
        while (fs::exists(dest, ec))
        {
            dest = fs::path(m_destDir) /
                   (src.stem().string() + "_" + std::to_string(++n) + src.extension().string());
        }
        fs::rename(src, dest, ec);
        if (ec)
        {
            rollback();
            m_lastError = "Cannot move file: " + p;
            return;
        }
        m_moved.emplace_back(p, dest.string());
    }
    m_executed = true;
}

void FileMoveCommand::undo()
{
    if (!canUndo())
        return;
    m_lastError.clear();
    std::error_code ec;
    std::vector<std::pair<std::string, std::string>> remaining;
    for (const auto &[orig, dest] : m_moved)
    {
        fs::path src(dest);
        fs::path back(orig);
        if (!fs::exists(src, ec))
            continue;
        fs::create_directories(back.parent_path(), ec);
        fs::rename(src, back, ec);
        if (ec)
        {
            remaining.emplace_back(orig, dest);
            continue;
        }
    }
    if (!remaining.empty())
    {
        m_moved = std::move(remaining);
        m_lastError = "Could not move some files back to their original location.";
        return;
    }
    m_moved.clear();
    m_executed = false;
}

void FileMoveCommand::rollback()
{
    std::error_code ec;
    for (auto it = m_moved.rbegin(); it != m_moved.rend(); ++it)
    {
        fs::path src(it->second);
        fs::path back(it->first);
        if (!fs::exists(src, ec))
            continue;
        fs::create_directories(back.parent_path(), ec);
        fs::rename(src, back, ec); // best-effort undo of partial progress
    }
    m_moved.clear();
}
