#pragma once

#include "FileSystemAdapter.h"
#include "ICommand.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Reversible multi-file copy into a destination directory.
// The source stays in place. Undo deletes only the copies this command made.
// Domain-free (core/command, no Qt).
class FileCopyCommand : public ICommand
{
  public:
    FileCopyCommand(std::vector<std::string> paths, std::string destDir,
                    std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem = {});

    std::string id() const override
    {
        return "file.copy";
    }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return (m_executed || m_unresolved) && !m_copied.empty();
    }
    bool canExecute() const override
    {
        return !m_paths.empty() && !m_destDir.empty();
    }

    const std::vector<std::pair<std::string, std::string>> &copied() const
    {
        return m_copied;
    }

    std::string lastError() const override
    {
        return m_lastError;
    }

    void setTransferObserver(mviewer::core::FileSystemAdapter::TransferObserver observer)
    {
        m_transferObserver = std::move(observer);
    }

    bool hasUnresolvedState() const override
    {
        return m_unresolved;
    }

    enum class State : std::uint8_t
    {
        Idle,
        Succeeded,
        Failed,
        RolledBack,
        RollbackFailed,
        Undone
    };

    State state() const
    {
        return m_state;
    }

  private:
    void rollback();
    bool removeCopiedFiles(const char *operation);
    void setFailure(const std::string &message);

    std::vector<std::string> m_paths;
    std::string m_destDir;
    // (sourcePath, copiedPath) for each copy that reached the destination.
    std::vector<std::pair<std::string, std::string>> m_copied;
    bool m_executed = false;
    bool m_unresolved = false;
    State m_state = State::Idle;
    std::shared_ptr<mviewer::core::FileSystemAdapter> m_fileSystem;
    mviewer::core::FileSystemAdapter::TransferObserver m_transferObserver;
    std::string m_lastError;
};
