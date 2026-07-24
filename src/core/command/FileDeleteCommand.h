#pragma once

#include "ICommand.h"

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
    FileDeleteCommand(std::vector<std::string> paths, std::string trashDir);

    std::string id() const override
    {
        return "file.delete";
    }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return m_executed && !m_moved.empty();
    }
    bool canExecute() const override
    {
        return !m_paths.empty() && !m_trashDir.empty();
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

  private:
    void rollback();

    std::vector<std::string> m_paths;
    std::string m_trashDir;
    // (originalPath, trashPath) for each successfully moved file.
    std::vector<std::pair<std::string, std::string>> m_moved;
    bool m_executed = false;
    std::string m_lastError;
};
