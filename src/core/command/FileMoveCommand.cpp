#include "FileMoveCommand.h"

#include <filesystem>

namespace fs = std::filesystem;

FileMoveCommand::FileMoveCommand(std::vector<std::string> paths, std::string destDir,
                                 std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem)
    : m_paths(std::move(paths)), m_destDir(std::move(destDir)),
      m_fileSystem(fileSystem ? std::move(fileSystem) : mviewer::core::defaultFileSystemAdapter())
{
}

std::string FileMoveCommand::description() const
{
    if (m_paths.size() == 1)
        return "Move " + mviewer::core::pathToUtf8(
                            mviewer::core::pathFromUtf8(m_paths.front()).filename());
    return "Move " + std::to_string(m_paths.size()) + " files";
}

void FileMoveCommand::setFailure(const std::string &message)
{
    m_lastError = message;
    m_state = State::Failed;
}

void FileMoveCommand::execute()
{
    if (!canExecute())
        return;

    m_moved.clear();
    m_lastError.clear();
    m_executed = false;
    m_unresolved = false;
    m_state = State::Idle;
    const fs::path destinationDirectory = mviewer::core::pathFromUtf8(m_destDir);
    std::error_code ec;
    if (m_fileSystem->exists(destinationDirectory, ec))
    {
        if (!m_fileSystem->isDirectory(destinationDirectory, ec))
        {
            setFailure("Destination is not a directory: " + m_destDir);
            return;
        }
    }
    else if (ec || !m_fileSystem->createDirectories(destinationDirectory, ec) || ec)
    {
        setFailure("Cannot create destination directory: " + m_destDir +
                   (ec ? ": " + ec.message() : std::string{}));
        return;
    }

    std::vector<std::pair<fs::path, fs::path>> plan;
    plan.reserve(m_paths.size());
    for (const auto &path : m_paths)
    {
        const fs::path source = mviewer::core::pathFromUtf8(path);
        if (!m_fileSystem->exists(source, ec) || !m_fileSystem->isRegularFile(source, ec))
        {
            setFailure("Source file is missing or not regular: " + path);
            return;
        }
        std::string destinationError;
        const fs::path destination = mviewer::core::collisionFreeDestination(
            source, destinationDirectory, m_fileSystem, destinationError);
        if (destination.empty())
        {
            setFailure(destinationError);
            return;
        }
        plan.emplace_back(source, destination);
    }

    for (const auto &[source, destination] : plan)
    {
        const auto result = mviewer::core::moveFileSafely(source, destination, m_fileSystem);
        if (result.state == mviewer::core::FileTransferState::Succeeded)
        {
            m_moved.emplace_back(mviewer::core::pathToUtf8(source),
                                 mviewer::core::pathToUtf8(destination));
            continue;
        }

        if (result.state == mviewer::core::FileTransferState::Partial)
            m_moved.emplace_back(mviewer::core::pathToUtf8(source),
                                 mviewer::core::pathToUtf8(destination));
        setFailure("Move failed for " + mviewer::core::pathToUtf8(source) + ": " + result.error);
        rollback();
        return;
    }
    m_executed = true;
    m_state = State::Succeeded;
}

bool FileMoveCommand::restoreMovedPaths(const char *operation)
{
    std::vector<std::pair<std::string, std::string>> remaining;
    std::string failure;
    for (auto it = m_moved.rbegin(); it != m_moved.rend(); ++it)
    {
        const fs::path original = mviewer::core::pathFromUtf8(it->first);
        const fs::path current = mviewer::core::pathFromUtf8(it->second);
        std::error_code ec;
        const bool currentExists = m_fileSystem->exists(current, ec);
        const bool originalExists = m_fileSystem->exists(original, ec);
        if (!currentExists && originalExists)
            continue;
        if (!currentExists || originalExists)
        {
            remaining.push_back(*it);
            failure = "path collision or missing moved file";
            continue;
        }

        const auto result = mviewer::core::moveFileSafely(current, original, m_fileSystem);
        if (result.state == mviewer::core::FileTransferState::Succeeded)
            continue;
        remaining.push_back(*it);
        failure = result.error;
    }
    m_moved.assign(remaining.rbegin(), remaining.rend());
    if (!m_moved.empty())
    {
        m_lastError += " " + std::string(operation) + " failed for " +
                       std::to_string(m_moved.size()) + " file(s): " + failure;
        return false;
    }
    return true;
}

void FileMoveCommand::rollback()
{
    const std::string originalError = m_lastError;
    if (restoreMovedPaths("Rollback"))
    {
        m_executed = false;
        m_unresolved = false;
        m_state = State::RolledBack;
        m_lastError = originalError + " All completed moves were rolled back.";
    }
    else
    {
        m_executed = true;
        m_unresolved = true;
        m_state = State::RollbackFailed;
        m_lastError = originalError + " Some files remain moved; use Undo to retry recovery.";
    }
}

void FileMoveCommand::undo()
{
    if (!canUndo())
        return;

    m_lastError.clear();
    if (restoreMovedPaths("Undo"))
    {
        m_moved.clear();
        m_executed = false;
        m_unresolved = false;
        m_state = State::Undone;
        return;
    }
    m_executed = true;
    m_unresolved = true;
    m_state = State::RollbackFailed;
}
