#include "FileRenameCommand.h"

#include <filesystem>

namespace fs = std::filesystem;

FileRenameCommand::FileRenameCommand(std::string oldPath, std::string newPath,
                                     std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem)
    : m_oldPath(std::move(oldPath)), m_newPath(std::move(newPath)),
      m_fileSystem(fileSystem ? std::move(fileSystem) : mviewer::core::defaultFileSystemAdapter())
{
}

std::string FileRenameCommand::description() const
{
    return "Rename " + mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(m_oldPath).filename()) +
           " → " +
           mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(m_newPath).filename());
}

void FileRenameCommand::execute()
{
    if (!canExecute())
        return;

    m_lastError.clear();
    m_executed = false;
    m_unresolved = false;
    const fs::path oldPath = mviewer::core::pathFromUtf8(m_oldPath);
    const fs::path newPath = mviewer::core::pathFromUtf8(m_newPath);
    std::error_code ec;
    if (!m_fileSystem->exists(oldPath, ec))
    {
        m_lastError = "Source file does not exist: " + m_oldPath;
        return;
    }
    if (!m_fileSystem->isRegularFile(oldPath, ec))
    {
        m_lastError = "Source is not a regular file: " + m_oldPath;
        return;
    }
    if (m_fileSystem->exists(newPath, ec))
    {
        m_lastError = "Destination already exists: " + m_newPath;
        return;
    }
    if (ec)
    {
        m_lastError = "Cannot inspect destination: " + m_newPath + ": " + ec.message();
        return;
    }

    const auto result = mviewer::core::moveFileSafely(oldPath, newPath, m_fileSystem);
    if (result.state == mviewer::core::FileTransferState::Succeeded)
    {
        m_executed = true;
        return;
    }

    m_lastError = "Rename failed: " + result.error;
    if (result.state == mviewer::core::FileTransferState::Partial)
    {
        m_executed = true;
        m_unresolved = true;
        m_lastError += " The source/destination state is retained for recovery.";
    }
}

void FileRenameCommand::undo()
{
    if (!canUndo())
        return;

    m_lastError.clear();
    const fs::path oldPath = mviewer::core::pathFromUtf8(m_oldPath);
    const fs::path newPath = mviewer::core::pathFromUtf8(m_newPath);
    std::error_code ec;
    if (m_fileSystem->exists(oldPath, ec))
    {
        m_lastError = "Original name is now occupied: " + m_oldPath;
        m_unresolved = true;
        return;
    }
    if (!m_fileSystem->exists(newPath, ec))
    {
        m_lastError = "Renamed file no longer exists: " + m_newPath;
        m_unresolved = true;
        return;
    }

    const auto result = mviewer::core::moveFileSafely(newPath, oldPath, m_fileSystem);
    if (result.state != mviewer::core::FileTransferState::Succeeded)
    {
        m_lastError = "Undo rename failed: " + result.error;
        m_unresolved = result.state == mviewer::core::FileTransferState::Partial;
        return;
    }
    m_executed = false;
    m_unresolved = false;
}
