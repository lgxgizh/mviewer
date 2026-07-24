#include "FileDeleteCommand.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

FileDeleteCommand::FileDeleteCommand(std::vector<std::string> paths, std::string trashDir)
    : m_paths(std::move(paths)), m_trashDir(std::move(trashDir))
{
}

std::string FileDeleteCommand::description() const
{
    if (m_paths.size() == 1)
        return "Delete " + fs::path(m_paths.front()).filename().string();
    return "Delete " + std::to_string(m_paths.size()) + " files";
}

void FileDeleteCommand::execute()
{
    if (!canExecute())
        return;
    m_moved.clear();
    m_lastError.clear();
    std::error_code ec;
    fs::create_directories(m_trashDir, ec);
    if (ec)
    {
        m_lastError = "Cannot create trash directory: " + m_trashDir;
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
        fs::path dest = fs::path(m_trashDir) / src.filename();
        // Avoid collisions in trash by appending a counter.
        int n = 0;
        while (fs::exists(dest, ec))
        {
            dest = fs::path(m_trashDir) /
                   (src.stem().string() + "_" + std::to_string(++n) + src.extension().string());
        }
        fs::rename(src, dest, ec);
        if (ec)
        {
            rollback();
            m_lastError = "Cannot move to trash: " + p;
            return;
        }
        m_moved.emplace_back(p, dest.string());
    }
    m_executed = true;
}

void FileDeleteCommand::undo()
{
    if (!canUndo())
        return;
    m_lastError.clear();
    std::error_code ec;
    std::vector<std::pair<std::string, std::string>> remaining;
    for (const auto &[orig, trash] : m_moved)
    {
        fs::path src(trash);
        fs::path dest(orig);
        if (!fs::exists(src, ec))
            continue; // Already restored or missing; treat as success.
        fs::create_directories(dest.parent_path(), ec);
        fs::rename(src, dest, ec);
        if (ec)
        {
            remaining.emplace_back(orig, trash);
            continue;
        }
    }
    if (!remaining.empty())
    {
        m_moved = std::move(remaining);
        m_lastError = "Could not restore some files from trash.";
        return;
    }
    m_moved.clear();
    m_executed = false;
}

void FileDeleteCommand::rollback()
{
    std::error_code ec;
    for (auto it = m_moved.rbegin(); it != m_moved.rend(); ++it)
    {
        fs::path src(it->second);
        fs::path dest(it->first);
        if (!fs::exists(src, ec))
            continue;
        fs::create_directories(dest.parent_path(), ec);
        fs::rename(src, dest, ec); // best-effort undo of partial progress
    }
    m_moved.clear();
}
