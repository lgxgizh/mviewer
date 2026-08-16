#include "FileDeleteCommand.h"

#include <filesystem>

namespace fs = std::filesystem;

FileDeleteCommand::FileDeleteCommand(std::vector<std::string> paths, std::string trashDir,
                                     std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem)
    : m_paths(std::move(paths)), m_trashDir(std::move(trashDir)),
      m_fileSystem(fileSystem ? std::move(fileSystem) : mviewer::core::defaultFileSystemAdapter())
{
}

std::string FileDeleteCommand::description() const
{
    if (m_paths.size() == 1)
        return "Delete " + mviewer::core::pathToUtf8(
                             mviewer::core::pathFromUtf8(m_paths.front()).filename());
    return "Delete " + std::to_string(m_paths.size()) + " files";
}

void FileDeleteCommand::setFailure(const std::string &message)
{
    m_lastError = message;
    m_state = State::Failed;
}

void FileDeleteCommand::execute()
{
    if (!canExecute())
        return;

    m_moved.clear();
    m_lastError.clear();
    m_executed = false;
    m_unresolved = false;
    m_state = State::Idle;
    const fs::path trashDirectory = mviewer::core::pathFromUtf8(m_trashDir);
    std::error_code ec;
    if (m_fileSystem->exists(trashDirectory, ec))
    {
        if (!m_fileSystem->isDirectory(trashDirectory, ec))
        {
            setFailure("Trash path is not a directory: " + m_trashDir);
            return;
        }
    }
    else if (ec || !m_fileSystem->createDirectories(trashDirectory, ec) || ec)
    {
        setFailure("Cannot create trash directory: " + m_trashDir +
                   (ec ? ": " + ec.message() : std::string{}));
        return;
    }

    std::vector<std::pair<fs::path, fs::path>> plan;
    std::vector<uintmax_t> planSizes;
    plan.reserve(m_paths.size());
    planSizes.reserve(m_paths.size());
    uintmax_t totalBytes = 0;
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
            source, trashDirectory, m_fileSystem, destinationError);
        if (destination.empty())
        {
            setFailure(destinationError);
            return;
        }
        plan.emplace_back(source, destination);
        const uintmax_t size = m_fileSystem->fileSize(source, ec);
        if (ec)
        {
            setFailure("Cannot inspect source size: " + path + ": " + ec.message());
            return;
        }
        planSizes.push_back(size);
        totalBytes += size;
    }

    uintmax_t completedBytes = 0;
    for (size_t index = 0; index < plan.size(); ++index)
    {
        const auto &[source, destination] = plan[index];
        mviewer::core::FileSystemAdapter::TransferObserver observer;
        if (m_transferObserver)
        {
            observer = [this, completedBytes, totalBytes](uintmax_t copied, uintmax_t total)
            {
                const uintmax_t fileTotal = total == 0 ? 0 : total;
                const uintmax_t batchTotal = totalBytes == 0 ? 0 : totalBytes;
                return m_transferObserver(completedBytes + copied,
                                          batchTotal == 0 ? fileTotal : batchTotal);
            };
        }
        const auto result =
            mviewer::core::moveFileSafely(source, destination, m_fileSystem, observer);
        if (result.state == mviewer::core::FileTransferState::Succeeded)
        {
            m_moved.emplace_back(mviewer::core::pathToUtf8(source),
                                 mviewer::core::pathToUtf8(destination));
            completedBytes += planSizes[index];
            continue;
        }

        if (result.state == mviewer::core::FileTransferState::Partial)
            m_moved.emplace_back(mviewer::core::pathToUtf8(source),
                                 mviewer::core::pathToUtf8(destination));
        setFailure("Delete failed for " + mviewer::core::pathToUtf8(source) + ": " + result.error);
        rollback();
        return;
    }
    m_executed = true;
    m_state = State::Succeeded;
}

bool FileDeleteCommand::restoreMovedPaths(const char *operation)
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
            failure = "path collision or missing trash entry";
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

void FileDeleteCommand::rollback()
{
    const std::string originalError = m_lastError;
    if (restoreMovedPaths("Rollback"))
    {
        m_executed = false;
        m_unresolved = false;
        m_state = State::RolledBack;
        m_lastError = originalError + " All completed deletes were rolled back.";
    }
    else
    {
        m_executed = true;
        m_unresolved = true;
        m_state = State::RollbackFailed;
        m_lastError = originalError + " Some files remain in trash; use Undo to retry recovery.";
    }
}

void FileDeleteCommand::undo()
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
