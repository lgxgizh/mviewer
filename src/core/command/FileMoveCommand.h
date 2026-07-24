#pragma once

#include "ICommand.h"

#include <string>
#include <utility>
#include <vector>

// Reversible multi-file move into a destination directory.
// Domain-free (core/command, no Qt).
class FileMoveCommand : public ICommand
{
  public:
    FileMoveCommand(std::vector<std::string> paths, std::string destDir);

    std::string id() const override
    {
        return "file.move";
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

  private:
    void rollback();

    std::vector<std::string> m_paths;
    std::string m_destDir;
    // (originalPath, newPath) for each successfully moved file.
    std::vector<std::pair<std::string, std::string>> m_moved;
    bool m_executed = false;
    std::string m_lastError;
};
