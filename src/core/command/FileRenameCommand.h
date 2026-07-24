#pragma once

#include "ICommand.h"

#include <string>

// Reversible single-file rename. Domain-free (core/command, no Qt).
class FileRenameCommand : public ICommand
{
  public:
    FileRenameCommand(std::string oldPath, std::string newPath);

    std::string id() const override
    {
        return "file.rename";
    }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return m_executed;
    }
    bool canExecute() const override
    {
        return !m_oldPath.empty() && !m_newPath.empty() && m_oldPath != m_newPath;
    }

    const std::string &oldPath() const
    {
        return m_oldPath;
    }
    const std::string &newPath() const
    {
        return m_newPath;
    }

    std::string lastError() const override
    {
        return m_lastError;
    }

  private:
    std::string m_oldPath;
    std::string m_newPath;
    bool m_executed = false;
    std::string m_lastError;
};
