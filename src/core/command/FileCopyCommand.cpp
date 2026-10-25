#include "FileCopyCommand.h"

#include <filesystem>

namespace fs = std::filesystem;

FileCopyCommand::FileCopyCommand(std::vector<std::string> paths, std::string destDir,
                                 std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem)
    : m_paths(std::move(paths)), m_destDir(std::move(destDir)),
      m_fileSystem(fileSystem ? std::move(fileSystem) : mviewer::core::defaultFileSystemAdapter())
{
}

std::string FileCopyCommand::description() const
{
    if (m_paths.size() == 1)
        return "Copy " +
               mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(m_paths.front()).filename());
    return "Copy " + std::to_string(m_paths.size()) + " files";
}

void FileCopyCommand::setFailure(const std::string &message)
{
    m_lastError = message;
    m_state = State::Failed;
}

void FileCopyCommand::execute()
{
    if (!canExecute())
        return;

    m_copied.clear();
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
            source, destinationDirectory, m_fileSystem, destinationError);
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
            mviewer::core::copyFileAtomically(source, destination, m_fileSystem, observer);
        if (result.state == mviewer::core::FileTransferState::Succeeded)
        {
            m_copied.emplace_back(mviewer::core::pathToUtf8(source),
                                  mviewer::core::pathToUtf8(destination));
            completedBytes += planSizes[index];
            continue;
        }

        if (result.state == mviewer::core::FileTransferState::Partial)
            m_copied.emplace_back(mviewer::core::pathToUtf8(source),
                                  mviewer::core::pathToUtf8(destination));
        setFailure("Copy failed for " + mviewer::core::pathToUtf8(source) + ": " + result.error);
        rollback();
        return;
    }
    m_executed = true;
    m_state = State::Succeeded;
}

bool FileCopyCommand::removeCopiedFiles(const char *operation)
{
    std::vector<std::pair<std::string, std::string>> remaining;
    std::string failure;
    for (auto it = m_copied.rbegin(); it != m_copied.rend(); ++it)
    {
        const fs::path copied = mviewer::core::pathFromUtf8(it->second);
        std::error_code ec;
        if (!m_fileSystem->exists(copied, ec))
            continue;
        if (m_fileSystem->remove(copied, ec) && !ec)
            continue;
        remaining.push_back(*it);
        failure = ec ? ec.message() : "remove failed";
    }
    m_copied.assign(remaining.rbegin(), remaining.rend());
    if (!m_copied.empty())
    {
        m_lastError += " " + std::string(operation) + " failed for " +
                       std::to_string(m_copied.size()) + " file(s): " + failure;
        return false;
    }
    return true;
}

void FileCopyCommand::rollback()
{
    const std::string originalError = m_lastError;
    if (removeCopiedFiles("Rollback"))
    {
        m_executed = false;
        m_unresolved = false;
        m_state = State::RolledBack;
        m_lastError = originalError + " All completed copies were rolled back.";
    }
    else
    {
        m_executed = true;
        m_unresolved = true;
        m_state = State::RollbackFailed;
        m_lastError = originalError + " Some copies remain; use Undo to retry recovery.";
    }
}

void FileCopyCommand::undo()
{
    if (!canUndo())
        return;

    m_lastError.clear();
    if (removeCopiedFiles("Undo"))
    {
        m_copied.clear();
        m_executed = false;
        m_unresolved = false;
        m_state = State::Undone;
        return;
    }
    m_executed = true;
    m_unresolved = true;
    m_state = State::RollbackFailed;
}
