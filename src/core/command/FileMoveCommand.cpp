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
    std::error_code ec;
    fs::create_directories(m_destDir, ec);
    for (const auto &p : m_paths)
    {
        fs::path src(p);
        if (!fs::exists(src, ec))
            continue;
        fs::path dest = fs::path(m_destDir) / src.filename();
        int n = 0;
        while (fs::exists(dest, ec))
        {
            dest = fs::path(m_destDir) /
                   (src.stem().string() + "_" + std::to_string(++n) + src.extension().string());
        }
        fs::rename(src, dest, ec);
        if (!ec)
            m_moved.emplace_back(p, dest.string());
    }
    m_executed = true;
}

void FileMoveCommand::undo()
{
    if (!canUndo())
        return;
    std::error_code ec;
    for (const auto &[orig, dest] : m_moved)
    {
        fs::path src(dest);
        fs::path back(orig);
        if (!fs::exists(src, ec))
            continue;
        fs::create_directories(back.parent_path(), ec);
        fs::rename(src, back, ec);
    }
    m_moved.clear();
    m_executed = false;
}
