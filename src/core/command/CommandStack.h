#pragma once

#include "ICommand.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ─── CommandStack ────────────────────────────────────────────────────────────
// Undo/redo history for the unified Command pattern (Architect P1-4). Every
// user action that should be reversible — Rotate, Crop, Compare, Label, Rename,
// Delete — is an ICommand; the stack runs execute() and records it so undo()
// can reverse it. Domain-free (core/command, no Qt).
//
// Bounded: `maxDepth` limits retained history (oldest undos are dropped).
// A change callback lets the UI refresh its Undo/Redo menu state.

class CommandStack
{
  public:
    explicit CommandStack(size_t maxDepth = 100) : m_maxDepth(maxDepth)
    {
    }

    // Execute a command and push it onto the undo history. Clears the redo
    // stack (a new action invalidates the redo branch). Takes ownership.
    // Returns false if the command reported an error; the command is then NOT
    // added to the undo history so it cannot be half-undone.
    bool execute(std::unique_ptr<ICommand> cmd)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!cmd || !cmd->canExecute())
        {
            m_lastError = "Command cannot be executed.";
            return false;
        }
        cmd->execute();
        if (!cmd->lastError().empty())
        {
            m_lastError = cmd->lastError();
            notify();
            return false;
        }
        m_undo.push_back(std::move(cmd));
        while (m_undo.size() > m_maxDepth)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        m_lastError.clear();
        notify();
        return true;
    }

    // Reverse the most recent command. Moves it to the redo stack on success.
    bool undo()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_undo.empty() || !m_undo.back()->canUndo())
        {
            m_lastError = "Nothing to undo.";
            return false;
        }
        auto &cmd = m_undo.back();
        cmd->undo();
        if (!cmd->lastError().empty())
        {
            m_lastError = cmd->lastError();
            notify();
            return false;
        }
        m_redo.push_back(std::move(cmd));
        m_undo.pop_back();
        m_lastError.clear();
        notify();
        return true;
    }

    // Re-apply the most recently undone command.
    bool redo()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_redo.empty())
        {
            m_lastError = "Nothing to redo.";
            return false;
        }
        auto &cmd = m_redo.back();
        cmd->execute();
        if (!cmd->lastError().empty())
        {
            m_lastError = cmd->lastError();
            notify();
            return false;
        }
        m_undo.push_back(std::move(cmd));
        m_redo.pop_back();
        m_lastError.clear();
        notify();
        return true;
    }

    bool canUndo() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return !m_undo.empty() && m_undo.back()->canUndo();
    }
    bool canRedo() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return !m_redo.empty();
    }

    std::string undoLabel() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_undo.empty() ? std::string{} : m_undo.back()->description();
    }
    std::string redoLabel() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_redo.empty() ? std::string{} : m_redo.back()->description();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_undo.clear();
        m_redo.clear();
        notify();
    }

    void setChangeCallback(std::function<void()> cb)
    {
        m_onChange = std::move(cb);
    }

    size_t undoDepth() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_undo.size();
    }
    size_t redoDepth() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_redo.size();
    }

    // Description of the last failed execute/undo/redo, empty on success.
    std::string lastError() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_lastError;
    }

  private:
    void notify()
    {
        if (m_onChange)
            m_onChange();
    }

    mutable std::mutex m_mtx;
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
    size_t m_maxDepth;
    std::function<void()> m_onChange;
    std::string m_lastError;
};
