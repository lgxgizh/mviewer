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
    std::error_code ec;
    fs::rename(m_oldPath, m_newPath, ec);
    m_executed = !ec;
}

void FileRenameCommand::undo()
{
    if (!canUndo())
        return;
    std::error_code ec;
    fs::rename(m_newPath, m_oldPath, ec);
    if (!ec)
        m_executed = false;
}
