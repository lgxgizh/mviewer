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
    std::error_code ec;
    fs::create_directories(m_trashDir, ec);
    for (const auto &p : m_paths)
    {
        fs::path src(p);
        if (!fs::exists(src, ec))
            continue;
        fs::path dest = fs::path(m_trashDir) / src.filename();
        // Avoid collisions in trash by appending a counter.
        int n = 0;
        while (fs::exists(dest, ec))
        {
            dest = fs::path(m_trashDir) /
                   (src.stem().string() + "_" + std::to_string(++n) + src.extension().string());
        }
        fs::rename(src, dest, ec);
        if (!ec)
            m_moved.emplace_back(p, dest.string());
        else
        {
            // Fallback: hard-delete if rename fails (cross-device).
            // Not reversible in that case — skip recording.
            fs::remove(src, ec);
        }
    }
    m_executed = true;
}

void FileDeleteCommand::undo()
{
    if (!canUndo())
        return;
    std::error_code ec;
    for (const auto &[orig, trash] : m_moved)
    {
        fs::path src(trash);
        fs::path dest(orig);
        if (!fs::exists(src, ec))
            continue;
        // Ensure parent directory still exists.
        fs::create_directories(dest.parent_path(), ec);
        fs::rename(src, dest, ec);
    }
    m_moved.clear();
    m_executed = false;
}
