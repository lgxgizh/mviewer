#pragma once

#include "ICommand.h"
#include "FileSystemAdapter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Reversible multi-file move into a destination directory.
// Domain-free (core/command, no Qt).
class FileMoveCommand : public ICommand
{
  public:
    FileMoveCommand(std::vector<std::string> paths, std::string destDir,
                    std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem = {});

    std::string id() const override
    {
        return "file.move";
    }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return (m_executed || m_unresolved) && !m_moved.empty();
    }
    bool canExecute() const override
    {
        return !m_paths.empty() && !m_destDir.empty();
    }

    const std::vector<std::pair<std::string, std::string>> &moved() const
    {
        return m_moved;
    }

    std::string lastError() const override
    {
        return m_lastError;
    }

    // Configured by the UI before a worker executes the command. Returning
    // false requests cancellation; the command still rolls back completed
    // files through its existing recovery path.
    void setTransferObserver(mviewer::core::FileSystemAdapter::TransferObserver observer)
    {
        m_transferObserver = std::move(observer);
    }

    bool hasUnresolvedState() const override
    {
        return m_unresolved;
    }

    enum class State
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
    bool restoreMovedPaths(const char *operation);
    void setFailure(const std::string &message);

    std::vector<std::string> m_paths;
    std::string m_destDir;
    // (originalPath, newPath) for each successfully moved file.
    std::vector<std::pair<std::string, std::string>> m_moved;
    bool m_executed = false;
    bool m_unresolved = false;
    State m_state = State::Idle;
    std::shared_ptr<mviewer::core::FileSystemAdapter> m_fileSystem;
    mviewer::core::FileSystemAdapter::TransferObserver m_transferObserver;
    std::string m_lastError;
};
