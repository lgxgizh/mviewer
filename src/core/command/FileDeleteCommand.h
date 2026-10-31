#pragma once

#include "FileSystemAdapter.h"
#include "ICommand.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Reversible multi-file delete: moves files into a trash directory on execute,
// and restores them on undo. Domain-free (core/command, no Qt).
//
// Paths are stored as UTF-8 std::string so the command stays Qt-free; the UI
// layer converts QString ↔ std::string at the boundary.
class FileDeleteCommand : public ICommand
{
  public:
    // `paths`  : absolute paths of files to delete
    // `trashDir`: absolute path of the trash directory (created if missing)
    FileDeleteCommand(std::vector<std::string> paths, std::string trashDir,
                      std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem = {});

    std::string id() const override
    {
        return "file.delete";
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
        // An empty private directory is allowed once the UI has installed a
        // system recycle-bin callback. execute() tries that before refusing.
        return !m_paths.empty() && (!m_trashDir.empty() || static_cast<bool>(m_systemTrash));
    }

    // Paths successfully moved to trash (original → trash path pairs).
    const std::vector<std::pair<std::string, std::string>> &moved() const
    {
        return m_moved;
    }

    std::string lastError() const override
    {
        return m_lastError;
    }

    // Configured by the UI before a worker executes the command. Returning
    // false requests cancellation; completed moves remain recoverable if
    // rollback itself encounters a fault.
    void setTransferObserver(mviewer::core::FileSystemAdapter::TransferObserver observer)
    {
        m_transferObserver = std::move(observer);
    }

    // Optional system recycle bin (Windows Recycle Bin / freedesktop Trash),
    // installed by the UI layer. Returns true and the file's new location
    // when the OS took the file; undo moves it back from there. When it is
    // unset or fails, the file goes to `trashDir` as before.
    using SystemTrash = std::function<bool(const std::string &source, std::string &pathInTrash)>;
    void setSystemTrash(SystemTrash systemTrash)
    {
        m_systemTrash = std::move(systemTrash);
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
    bool moveToSystemTrash(const std::filesystem::path &source);
    bool restoreMovedPaths(const char *operation);
    void setFailure(const std::string &message);

    std::vector<std::string> m_paths;
    std::string m_trashDir;
    SystemTrash m_systemTrash;
    // (originalPath, trashPath) for each successfully moved file.
    std::vector<std::pair<std::string, std::string>> m_moved;
    bool m_executed = false;
    bool m_unresolved = false;
    State m_state = State::Idle;
    std::shared_ptr<mviewer::core::FileSystemAdapter> m_fileSystem;
    mviewer::core::FileSystemAdapter::TransferObserver m_transferObserver;
    std::string m_lastError;
};
