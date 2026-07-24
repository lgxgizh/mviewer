#include "FileRenameCommand.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

FileRenameCommand::FileRenameCommand(std::string oldPath, std::string newPath)
    : m_oldPath(std::move(oldPath)), m_newPath(std::move(newPath))
{
}

std::string FileRenameCommand::description() const
{
    return "Rename " + fs::path(m_oldPath).filename().string() + " → " +
           fs::path(m_newPath).filename().string();
}

void FileRenameCommand::execute()
{
    if (!canExecute())
        return;
    m_lastError.clear();
    std::error_code ec;
    if (!fs::exists(m_oldPath, ec))
    {
        m_lastError = "Source file does not exist: " + m_oldPath;
        return;
    }
    if (fs::exists(m_newPath, ec))
    {
        m_lastError = "Destination already exists: " + m_newPath;
        return;
    }
    fs::rename(m_oldPath, m_newPath, ec);
    if (ec)
    {
        m_lastError = "Rename failed: " + ec.message();
        return;
    }
    m_executed = true;
}

void FileRenameCommand::undo()
{
    if (!canUndo())
        return;
    m_lastError.clear();
    std::error_code ec;
    if (!fs::exists(m_newPath, ec))
    {
        m_lastError = "Renamed file no longer exists: " + m_newPath;
        return;
    }
    if (fs::exists(m_oldPath, ec))
    {
        m_lastError = "Original name is now occupied: " + m_oldPath;
        return;
    }
    fs::rename(m_newPath, m_oldPath, ec);
    if (ec)
    {
        m_lastError = "Undo rename failed: " + ec.message();
        return;
    }
    m_executed = false;
}
