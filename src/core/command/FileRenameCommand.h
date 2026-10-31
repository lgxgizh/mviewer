#pragma once

#include "FileSystemAdapter.h"
#include "ICommand.h"

#include <filesystem>
#include <memory>
#include <string>

// Reversible single-file rename. Domain-free (core/command, no Qt).
class FileRenameCommand : public ICommand
{
  public:
    FileRenameCommand(std::string oldPath, std::string newPath,
                      std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem = {});

    std::string id() const override
    {
        return "file.rename";
    }
    std::string description() const override;
    void execute() override;
    void undo() override;
    bool canUndo() const override
    {
        return m_executed || m_unresolved;
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

    bool hasUnresolvedState() const override
    {
        return m_unresolved;
    }

  private:
    // Two-step rename used when the names differ only by case and both paths
    // are the same file (Windows). `failurePrefix` is an existing file-op phrase.
    bool commitCaseOnlyRename(const std::filesystem::path &from, const std::filesystem::path &to,
                              const char *failurePrefix);

    std::string m_oldPath;
    std::string m_newPath;
    bool m_executed = false;
    bool m_unresolved = false;
    std::shared_ptr<mviewer::core::FileSystemAdapter> m_fileSystem;
    std::string m_lastError;
    // Set when a case-only rename stops on its temporary name. Undo moves that
    // file back to m_oldPath.
    std::string m_caseRenameTemp;
};
